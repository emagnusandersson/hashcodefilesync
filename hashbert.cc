#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h> // strncpy
#include <libgen.h> // basename

#include <iostream>
#include <vector>
#include <algorithm>    // std::sort
#include <string>       // std::to_string
#include <sstream>
#include <boost/algorithm/string.hpp>
#include <chrono>
#include <errno.h>

#if defined(__APPLE__)
#  define COMMON_DIGEST_FOR_OPENSSL
#  include <CommonCrypto/CommonDigest.h>
#  define SHA1 CC_SHA1
#else
#  include <openssl/md5.h>
#endif

#define LENFILENAME 1024


//
// How to compile:
//

// Compile: g++ -o hashbert hashbert.cc -lcrypto -std=gnu++11 -O3
// Compile for gdb: g++ -g -o hashbert hashbert.cc -lcrypto -std=gnu++11

// 
// How to debug (typical debug session):
//

// gdb hashbert
// break 365
// run [arguments]
// list      // List the code
// finish    // Step out of function


// Google for "ansi vt100 codes" to learn about the codes in the printf-strings

int calcHashOfFile(const char *filename, char out[33]) {
  int i, nRead;
  MD5_CTX ctx;
  unsigned char digest[16];
  FILE *fpt = fopen (filename, "rb");
  unsigned char data[1024];
  
  if(fpt == NULL) {    return 1;  }
  
  MD5_Init(&ctx);
  while ((nRead = fread (data, 1, 1024, fpt)) != 0)
    MD5_Update (&ctx, data, nRead);
  MD5_Final(digest, &ctx);
  fclose(fpt);
  for (i = 0; i < 16; ++i) {
    snprintf(&(out[i*2]), 3, "%02x", (unsigned int)digest[i]);  // Each "write" will write 3 bytes (2 + 1 null)
  }
  return 0;

}

unsigned int nFile=0; // Keeping track of how many files are found
void writeFContentToTmpFile(FILE *fpt, const std::string strDir){  // Write folder content (that is files only) of strDir. For folders in strDir recursive calls are made.
  DIR *dir;
  struct dirent *entry;
  struct EntryT {
    std::string str;
    char boDir;
    EntryT(const std::string& s, const char c) : str(s), boDir(c) {}
    bool operator < (const EntryT& structA) const {
        return str.compare(structA.str)<0;
    }
  };
  
  std::vector<EntryT> vecEntry;
     
  if (!(dir = opendir(strDir.c_str())))        return;
  
  while(1) {
    if (!(entry = readdir(dir))) break;
    vecEntry.push_back(EntryT(entry->d_name, entry->d_type == DT_DIR));
  }
  std::sort(std::begin(vecEntry), std::end(vecEntry));

  for(std::vector<EntryT>::iterator it=vecEntry.begin(); it!=vecEntry.end(); ++it) {
  //for(std::vector<EntryT>::size_type i = 0; i != vecEntry.size(); i++) {
    std::string strPath;
    EntryT ent=*it;

    strPath=strDir; strPath.append("/").append(ent.str);
    if (ent.boDir) {
      if (strcmp(ent.str.c_str(), ".") == 0 || strcmp(ent.str.c_str(), "..") == 0)     continue;
      writeFContentToTmpFile(fpt, strPath);
    }
    else {
      struct stat st;
      if(stat(strPath.c_str(), &st) != 0) perror(strPath.c_str());
      fprintf(fpt, "%d %d %s\n", st.st_mtim.tv_sec, st.st_size, strPath.c_str()); nFile++;
    }
  } 
  closedir(dir);
}
std::chrono::steady_clock::time_point tStart = std::chrono::steady_clock::now();
int toc(int &nHour, int &nMin, int &nSec){
  std::chrono::steady_clock::time_point tNow= std::chrono::steady_clock::now();
  int tDur=std::chrono::duration_cast<std::chrono::seconds> (tNow-tStart).count();
  nSec=tDur%60;
  nMin=tDur/60;
  nHour=(nMin/60)%60; nMin=nMin%60;
}
int createNewFile(FILE *fptNew, FILE *fptOld, FILE *fptTmp){  // Merge fptOld and fptTmp to fptNew and recalculate hashcode for new files and when file-mod-time/size is different
  char strHashOld[33], strHashTmp[33], *strHash;
  char boOldGotStuff=1, boTmpGotStuff=1;
  char boMakeReadOld=1, boMakeReadTmp=1;
  unsigned int nRow=0, nReused=0, nRecalc=0, nDelete=0, nNew=0;
  int intTimeOld, intSizeOld, intTimeTmp, intSizeTmp;
  char strFileOld[LENFILENAME], strFileTmp[LENFILENAME];
  //char strStatus[]="\033[F\r\033[2KWrote row: \033[1m%d/%d\033[0m, reused: \033[1m%d\033[0m, different modTime/size: \033[1m%d\033[0m, deleted: \033[1m%d\033[0m, new: \033[1m%d\033[0m  (file: \033[1m%s\033[0m)\n";
  //char strStatus[]="Row: \033[1m%d/%d\033[0m, reused: \033[1m%d\033[0m, different modTime/size: \033[1m%d\033[0m, deleted: \033[1m%d\033[0m, new: \033[1m%d\033[0m\n";
  char strStatus[]="%d:%02d:%02d, Writing row: \033[1m%d/%d\033[0m, reused: \033[1m%d\033[0m, different modTime/size: \033[1m%d\033[0m, deleted: \033[1m%d\033[0m, new: \033[1m%d\033[0m\n";
    
    
  enum action { NONE, DONE, REMOVE, REUSE, RECALCULATE, NEWFILE };
  enum action myAction=NONE;
  while(1) {
    int intCmp;
    if(boMakeReadOld) { if(fscanf(fptOld, "%32s %d %d %1023[^\n]\n", strHashOld, &intTimeOld, &intSizeOld, strFileOld)!=4) boOldGotStuff=0;     boMakeReadOld=0;  }
    if(boMakeReadTmp) { if(fscanf(fptTmp, "%d %d %1023[^\n]\n", &intTimeTmp, &intSizeTmp, strFileTmp)!=3) boTmpGotStuff=0;     boMakeReadTmp=0;  }
    
    
    myAction=NONE;
    if(boOldGotStuff && boTmpGotStuff){
      intCmp=strcmp(strFileOld, strFileTmp);
      if(intCmp==0){
        if(intTimeOld==intTimeTmp && intSizeOld==intSizeTmp) { myAction=REUSE; }
        else { myAction=RECALCULATE; }
        boMakeReadOld=1; boMakeReadTmp=1; nRow++;
      }
      else if(intCmp<0){ // The row exist in fptOld but not in fptTmp
        myAction=REMOVE; boMakeReadOld=1;
      }
      else if(intCmp>0){ // The row exist in fptTmp but not in fptOld
        myAction=NEWFILE; boMakeReadTmp=1; nRow++;
      }
    }else if(boOldGotStuff){ // Ending (obsolete) row(s) in fptOld
      myAction=REMOVE; boMakeReadOld=1; 
    }else if(boTmpGotStuff){ // Ending (new) row(s) in fptTmp
      myAction=NEWFILE; boMakeReadTmp=1; nRow++;
    }else { myAction=DONE;  }
    
    printf("\033[u\033[J"); // Reset cursor and clear everything below
    if(myAction==REUSE){ printf("Reusing hash: "); nReused++; strHash=strHashOld; }
    else if(myAction==RECALCULATE){ printf("Recalculating hash: "); nRecalc++; strHash=strHashTmp; }
    else if(myAction==REMOVE){ printf("File removed: "); nDelete++; }
    else if(myAction==NEWFILE){ printf("Calculating hash (New file): "); nNew++; strHash=strHashTmp; }
    else if(myAction==DONE){ printf("Done: "); }
    //else{ errno=ENAVAIL; printf("Error myAction has weird value (%d), (%s)\n", myAction, strFileTmp); return 1; }
    int nHour, nMin, nSec;    toc(nHour, nMin, nSec);
    //printf(strStatus, nRow, nFile, nReused, nRecalc, nDelete, nNew);
    printf(strStatus, nHour, nMin, nSec, nRow, nFile, nReused, nRecalc, nDelete, nNew);
     
    if(myAction>=RECALCULATE ){
      printf("(%s)\n", strFileTmp);
      if(calcHashOfFile(strFileTmp, strHash)) {perror(strFileTmp); return 1;}
    }
    if(myAction>=REUSE){   fprintf(fptNew, "%s %d %d %s\n", strHash, intTimeTmp, intSizeTmp, strFileTmp); }
    
    if(myAction==DONE){ break; }
      
  }
  return 0;
}

struct RuleT {
  std::string str;
  char boInc;
  RuleT() : str(""), boInc(1) {}
  RuleT(const std::string& s, const char c) : str(s), boInc(c) {}
  bool operator < (const RuleT& structA) const {
      return str.compare(structA.str)<0;
  }
};

std::vector<RuleT> vecRule;
char boIncDefault=1;
char boIncAssigned=2; // 2== not assigned

std::vector<std::string> split(const std::string &s, char delim) {
    std::stringstream ss(s);
    std::string item;
    std::vector<std::string> tokens;
    while (getline(ss, item, delim)) {
        tokens.push_back(item);
    }
    return tokens;
}
std::string trimwhitespace(std::string str){
  int iStart=0, iEnd, len=str.length();
  for(int i=0;i<len;i++){ if(!isspace(str[i])) {iStart=i; break;} }   // iStart
  if(iStart==len) return std::string("");
  for(int i=len-1;i>=0;i--){ if(!isspace(str[i])) {iEnd=i+1; break;} }   // iEnd (first position of the ending whitespace trail)
  return str.substr(iStart,iEnd-iStart);
}
int interpreteFilterRow(std::string strRow, RuleT &rule){  // returns: 0=success, 1=nothing found (comment or empty line) 
  //std::string strPattTmp;
  std::string strPattTmp;
  char boInc=1;
  //char *strTrimed=trimwhitespace(strRow);
  strRow=trimwhitespace(strRow);
  int lenTmp=strRow.length();
  //if(strlen(strTrimed)==1) {if(strTrimed[0]=='-') boIncDefault=0; else if(strTrimed[0]=='+') boIncDefault=1; 
    
  if(lenTmp){
    if(strRow[0]=='+') {boInc=1; strPattTmp=strRow.substr(1); }
    else if(strRow[0]=='-') { boInc=0; strPattTmp=strRow.substr(1); }
    else if(strRow[0]=='#') { return 1; }
    else  { boInc=1; strPattTmp=strRow; }
  } else return 1;
  //boost::trim(strPattTmp); 
  strPattTmp=trimwhitespace(strPattTmp);
  //int lenTmp=strlen(strPattTmp);
  //vecRule.push_back(RuleT(strPattTmp.c_str(), boInc));
  rule.str=strPattTmp.c_str(); rule.boInc=boInc;
  //vecRule.push_back(RuleT(strPattTmp, boInc));
  return 0;
}
int readFilterFrCommandLine(std::string strLine) {
  int i, nRead;
  std::vector<std::string> vecStr=split(strLine, ';');
  int nRule=vecStr.size();
  for(int i=0;i<nRule;i++) {
    RuleT ruleTmp;
    if(interpreteFilterRow(vecStr[i], ruleTmp)==0){
      if(ruleTmp.str.length()) vecRule.push_back(ruleTmp); else boIncAssigned=ruleTmp.boInc;
    }
  }
  return 0;
}
int readFilterFile(const std::string filename) {
  int i, nRead;
  FILE *fpt = fopen (filename.c_str(), "rb");
  char strRule[1024];  
  if(fpt == NULL) {    return 1;  }
  
  while(1) {
    int intCmp;
    char boGotStuff;
    if(fscanf(fpt, "%1023[^\n]\n", strRule)!=1) boGotStuff=0; else boGotStuff=1;
    if(boGotStuff){
      RuleT ruleTmp;
      //RuleT ruleTmp=RuleT("",1);
      if(interpreteFilterRow(std::string(strRule), ruleTmp)==0){
        if(ruleTmp.str.length()) vecRule.push_back(ruleTmp); else boIncAssigned=ruleTmp.boInc;
      }
    }else{break;}
  }
  fclose(fpt);
  return 0;
}


//void check(FILE *fpt){  // Go through the hashcode-file, for each file, check if the hashcode matches the actual files hashcode
int check(std::string strFileChk){  // Go through the hashcode-file, for each file, check if the hashcode matches the actual files hashcode  
  char strHash[33];
  unsigned int nRowCount=0, nNotFound=0, nMissMatchTimeSize=0, nMissMatchHash=0, nOK=0;
  //printf("OK\n");
  
  if(access(strFileChk.c_str(), F_OK)==-1) {perror(""); return 1;}
  FILE *fpt;
  fpt = fopen(strFileChk.c_str(), "r");
  //fclose(fptO);

    
  printf("\n\n\033[2A\033[s");  // 2 newlines (makes it scroll if you are on the bottom line), then go up 2 rows, then save cursor
  int nRule=vecRule.size();
  if(boIncAssigned!=2) boIncDefault=boIncAssigned;  // If "boIncDefault" is assigned by the user
  else { 
    char nInc=0, nExc=0;
    for(int i=0;i<nRule;i++) {
      RuleT rule=vecRule[i]; 
      //if(i==0)  {boIncIni=rule.boInc;}
      //if(rule.boInc!=boIncIni)  {boIncDefault=1; break;}
      if(rule.boInc) nInc++; else nExc++;
    }
    //if(nInc && nExc) boIncDefault=1; else if(nInc) boIncDefault=0; else if(nExc) boIncDefault=1; else boIncDefault=1;
    if(nInc && !nExc) boIncDefault=0; else if(!nInc && nExc) boIncDefault=1; else boIncDefault=1;
  }
  while(1) {
    int intTimeOld, intSizeOld;
    char boGotStuff=1;
    char strHashOld[33], strFile[LENFILENAME];
    if(fscanf(fpt,"%32s %d %d %1023[^\n]",strHashOld, &intTimeOld, &intSizeOld, strFile)!=4) boGotStuff=0;
    int nHour, nMin, nSec;
    if(boGotStuff){
      char boInc=boIncDefault;
      for(int i=0;i<nRule;i++) {
        std::string strPath;
        RuleT rule=vecRule[i]; 
        //if(strncmp(rule.str.c_str(), strFile, intRuleLen)!=0)  continue;
        if(rule.str.compare(0, std::string::npos, strFile, rule.str.length())==0)  {boInc=rule.boInc; break;}
      }
      if(boInc==0) continue;
      //std::chrono::steady_clock::time_point tNow= std::chrono::steady_clock::now();
      //int tDur=std::chrono::duration_cast<std::chrono::seconds> (tNow-tStart).count();
      
      toc(nHour, nMin, nSec);
      //printf("\033[u\033[JChecking row: %d (%s)\n", nRowCount+1, strFile);
      //printf("\033[u\033[JTime:%d, Checking row: %d (%s)\n", tDur, nRowCount+1, strFile);
      printf("\033[u\033[J%d:%02d:%02d, Checking row: %d (%s)\n", nHour, nMin, nSec, nRowCount+1, strFile);
      //if(calcHashOfFile(strFile,strHash)) {perror(strFile); return;}
      int boErr=calcHashOfFile(strFile,strHash);
      if(boErr) {
        int errnoTmp = errno;
        if(errnoTmp==ENOENT) {
          printf("\033[u\033[JNo such entry: %s\n", strFile);
          printf("\n\n\033[2A\033[s");  // Save cursor
          nNotFound++;
        }
        else {perror(strFile); return 1; }
      }
      else{
        if(strncmp(strHashOld, strHash, 32)!=0){
          
          
            // Check modTime and size (perhaps the user forgott to run sync before running check
          struct stat st;
          if(stat(strFile, &st) != 0) perror(strFile);
          if(st.st_mtim.tv_sec!=intTimeOld || st.st_size!=intSizeOld ){
            if(strcmp(basename(strFile), strFileChk.c_str())==0) {
              printf("\033[u\033[JMismatch (time/size)  (but this is expected for this file), %s\n", strFile);
              printf("\n\n\033[2A\033[s");  // Save cursor
            }else{    
              printf("\033[u\033[JMismatch (time/size)  (sync was not called), %s\n", strFile);
              printf("\n\n\033[2A\033[s");  // Save cursor
            }
            nMissMatchTimeSize++;
          }else{          
            printf("\033[u\033[JMismatch, old=%s, new=%s, %s\n", strHashOld, strHash, strFile);
            printf("\n\n\033[2A\033[s");  // Save cursor
            nMissMatchHash++;
          }
        }
        else nOK++;
      }
      nRowCount++;
    }else {
      toc(nHour, nMin, nSec);
      printf("\033[u\033[JTime:%d:%02d:%02d, Done (nRowCount %d, nNotFound %d, nMissMatchTimeSize %d, nMissMatchHash %d, nOK %d)\n", nHour, nMin, nSec, nRowCount, nNotFound, nMissMatchTimeSize, nMissMatchHash, nOK);
      break;
    }
  }
  return 0;
}

int countDash(const char *str){
  if(str[0]=='-' && str[1]=='-') return 2;       else if(str[0]=='-') return 1;       else return 0;
}
void helpTextExit( int argc, char **argv){
  printf("Help text: see https://emagnusandersson.com/hashbert\n", argv[0]);
  exit(0);
}
//#include <boost/algorithm/string.hpp>
int main( int argc, char **argv){
  int i;
  char boCheck=0;
  std::string strDir=".";
  //std::string strFileChk, strFileTmp, strFileNew;     strFileChk=strFileTmp=strFileNew="hashcodes.txt";
  std::string strFileChk, strFileNew;     strFileChk=strFileNew="hashcodes.txt";
  if(argc==1){ helpTextExit(argc, argv); }
  if(argc>1){
    if(strcmp(argv[1],"sync")==0) boCheck=0;
    else if(strcmp(argv[1],"check")==0) boCheck=1;
    else if(strcmp(argv[1],"--help")==0 || strcmp(argv[1],"-h")==0) helpTextExit(argc, argv);
    else helpTextExit(argc, argv);
  }
  for(i=2;i<argc;i++){
    int nDash=countDash(argv[i]);
    if(nDash==1) {
      char tmpc=argv[i][1];
      if(tmpc=='r') {
        //if(i+1>=argc || countDash(argv[i+1])) {printf("-r should be followed by filter rules. Like \"+./inclDir;-./exclDir\". Delimited by semicolons \";\" and wrapped by quotation marks (\"\").\n"); exit(0); }
        if(boCheck==0) {printf("Filtering is only available when \"check\"-ing\n"); exit(0); }
        if(strlen(argv[i])<=2) {printf("The -r option should have the rules comming directly after it (that is no space) Ex: -r+./myDir or \"-r-./exclDir;+./inclDir\"\n"); exit(0); }
        //Don't let the string start with \"-\" (You can use a \";\" in front of it for example. (sorry for this inconsistency)).
        else {
          //readFilterFrCommandLine(argv[i+1]);
          readFilterFrCommandLine(&(argv[i][2]));
          /*RuleT ruleTmp;
          if(interpreteFilterRow(argv[i+1], ruleTmp)==0){
            if(ruleTmp.str.length()) vecRule.push_back(ruleTmp); else boIncDefault=ruleTmp.boInc;
          }*/
          i++;
        }
      }else if(tmpc=='F') {
        std::string strFileFilter="";
        if(boCheck==0) {printf("Filtering is only available when \"check\"-ing\n"); exit(0); }
        if(i+1>=argc || countDash(argv[i+1])) { strFileFilter=".hashbert_filter"; }
        else {   strFileFilter=argv[i+1]; i++;  }
        //readFilterFile(strFileFilter);
        if(readFilterFile(strFileFilter)) {perror("Error in readFilterFile"); return 1;}
      }else if(tmpc=='f') {
        if(i+1>=argc || countDash(argv[i+1])) {printf("-f should be followed by the hashcode-file\n"); exit(0); }
        //strFileChk=strFileTmp=strFileNew=argv[i+1]; i++;
        strFileChk=strFileNew=argv[i+1]; i++;
      }else if(tmpc=='d') {
        if(i+1>=argc || countDash(argv[i+1])) {printf("-d should be followed by a directory\n"); exit(0); }
        strDir=argv[i+1];  i++;
      }else if(tmpc=='h') helpTextExit(argc, argv);
    }else if(nDash==2) {
      char *tmp=&argv[i][2];
      if(strcmp(tmp,"help")==0) helpTextExit(argc, argv);
    }
  }

  //strFileTmp.append(".filesonly.tmp");
  strFileNew.append(".new.tmp");
  
  if(boCheck){
    //FILE *fptOld;
    //if(access(strFileChk.c_str(), F_OK)==-1) {perror(""); return 1;}
    //fptOld = fopen(strFileChk.c_str(), "r");
    //check(fptOld);
    //fclose(fptOld);
    
    if(check(strFileChk)) {perror("Error in check"); return 1;}
  }else{
    int fd;
    char strFileTmp[] = "/tmp/fileXXXXXX";
    //FILE *fptTmp =mkstemp(strFileTmp);
    FILE *fptTmp=tmpfile();
    //FILE *fptTmp = fopen(strFileTmp.c_str(), "w+");

    printf("\n\n\n\n\033[4A");  // 4 newlines (makes it scroll if you are on the bottom line), then go up 4 rows
    printf("Reading filenames, modification dates and file sizes from the selected folder.\n\033[s");  // Save cursor at the end
    writeFContentToTmpFile(fptTmp, strDir);
    FILE *fptOld = fopen(strFileChk.c_str(), "a+");  // By using "a+" (reading and appending) the file is created if it doesn't exist.
    FILE *fptNew = fopen(strFileNew.c_str(), "w");
    rewind(fptTmp);
    if(createNewFile(fptNew, fptOld, fptTmp)) {perror("Error in createNewFile"); return 1;}
    fclose(fptTmp); fclose(fptOld); fclose(fptNew);
    //if(remove(strFileTmp.c_str()))  perror(strFileTmp.c_str());
    //if(remove(strFileTmp))  perror(strFileTmp);
    
   
    if(rename(strFileNew.c_str(), strFileChk.c_str())) perror("");
  }
  return 0;
}



