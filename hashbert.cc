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
#include <algorithm>    // std::sort, std::find
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
// Compile with:
//   g++ -o hashbert hashbert.cc -lcrypto -std=gnu++17 -O3
// Compile for gdb:
//   g++ -g -o hashbert hashbert.cc -lcrypto -std=gnu++17

// 
// How to debug (typical debug session):
//

// gdb hashbert
// b 365          // set breakpoint
// r [arguments]  // run 
// l              // List the code
// p [variable]   // Print variable
// finish         // Step out of function
// i b   // list breakpoints


  // Google for "ansi vt100 codes" to learn about the codes in the printf-strings
#define ANSI_CURSOR_SAVE        "\0337"
#define ANSI_CURSOR_RESTORE     "\0338"
#define ANSI_FONT_CLEAR         "\033[0m"
#define ANSI_FONT_BOLD          "\033[1m"
#define ANSI_CLEAR_BELOW        "\033[J"
#define ANSI_CURSOR_UP(n)      "\033[" #n "A"


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

    //strPath=strDir; 
    if(strDir==".") strPath=ent.str;
    else {strPath=strDir; strPath.append("/").append(ent.str); }
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
void toc(int &nHour, int &nMin, int &nSec){
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
  //char strStatus[]="\033[F\r\033[2KWrote row: " ANSI_FONT_BOLD "%d/%d" ANSI_FONT_CLEAR ", reused: " ANSI_FONT_BOLD "%d" ANSI_FONT_CLEAR ", different modTime/size: " ANSI_FONT_BOLD "%d" ANSI_FONT_CLEAR ", deleted: " ANSI_FONT_BOLD "%d" ANSI_FONT_CLEAR ", new: " ANSI_FONT_BOLD "%d" ANSI_FONT_CLEAR "  (file: " ANSI_FONT_BOLD "%s" ANSI_FONT_CLEAR ")\n";
  //char strStatus[]="Row: " ANSI_FONT_BOLD "%d/%d" ANSI_FONT_CLEAR ", reused: " ANSI_FONT_BOLD "%d" ANSI_FONT_CLEAR ", different modTime/size: " ANSI_FONT_BOLD "%d" ANSI_FONT_CLEAR ", deleted: " ANSI_FONT_BOLD "%d" ANSI_FONT_CLEAR ", new: " ANSI_FONT_BOLD "%d" ANSI_FONT_CLEAR "\n";
  char strStatus[]="%d:%02d:%02d, Writing row: " ANSI_FONT_BOLD "%d/%d" ANSI_FONT_CLEAR ", reused: " ANSI_FONT_BOLD "%d" ANSI_FONT_CLEAR ", different modTime/size: " ANSI_FONT_BOLD "%d" ANSI_FONT_CLEAR ", deleted: " ANSI_FONT_BOLD "%d" ANSI_FONT_CLEAR ", new: " ANSI_FONT_BOLD "%d" ANSI_FONT_CLEAR "\n";
    
    
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
    
    printf(ANSI_CURSOR_RESTORE ANSI_CLEAR_BELOW); // Reset cursor and clear everything below
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
  strRow = strRow.substr(0, strRow.find("#"));
  strRow=trimwhitespace(strRow);
  int lenTmp=strRow.length();
  //if(strlen(strTrimed)==1) {if(strTrimed[0]=='-') boIncDefault=0; else if(strTrimed[0]=='+') boIncDefault=1; 
    
  if(lenTmp){
    if(strRow[0]=='+') {boInc=1; strPattTmp=strRow.substr(1); }
    else if(strRow[0]=='-') { boInc=0; strPattTmp=strRow.substr(1); }
    //else if(strRow[0]=='#') { return 1; }
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
int check(std::string strFileChk, int intStart){  // Go through the hashcode-file, for each row (file), check if the hashcode matches the actual files hashcode  
  char strHash[33];
  unsigned int iRowCount=0, nNotFound=0, nMissMatchTimeSize=0, nMissMatchHash=0, nOK=0;
  //printf("OK\n");
  
  if(access(strFileChk.c_str(), F_OK)==-1) {perror(""); return 1;}
  FILE *fpt;
  fpt = fopen(strFileChk.c_str(), "r");
  //fclose(fptO);

    
  printf("\n\n" ANSI_CURSOR_UP(2) ANSI_CURSOR_SAVE);  // 2 newlines (makes it scroll if you are on the bottom line), then go up 2 rows, then save cursor
  
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
      iRowCount++;
      if(iRowCount<intStart) { continue;}  // iRowCount / intStart (row number) is 1-indexed
        // Test if the file is to be included (against rules)
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
      printf(ANSI_CURSOR_RESTORE ANSI_CLEAR_BELOW "%d:%02d:%02d, Checking row: %d (%s)\n", nHour, nMin, nSec, iRowCount, strFile);
      int boErr=calcHashOfFile(strFile,strHash);
      if(boErr) {
        int errnoTmp = errno;
        if(errnoTmp==ENOENT) {
          printf(ANSI_CURSOR_RESTORE ANSI_CLEAR_BELOW "Row:%d, ENOENT (file not found): %s\n", iRowCount, strFile);
          printf("\n\n" ANSI_CURSOR_UP(2) ANSI_CURSOR_SAVE);  // Save cursor
          nNotFound++;
        }
        else {perror(strFile); return 1; }
      }
      else{
        if(strncmp(strHashOld, strHash, 32)!=0){
          
          
            // Check modTime and size (perhaps the user forgott to run sync before running check
          struct stat st;
          if(stat(strFile, &st) != 0) perror(strFile);
          int boTMatch=st.st_mtim.tv_sec==intTimeOld, boSizeMatch=st.st_size==intSizeOld;
          std::string strTmp=ANSI_CURSOR_RESTORE ANSI_CLEAR_BELOW "Row:%d, Mismatch: ";
          if(!boTMatch || !boSizeMatch ){
            if(!boTMatch) strTmp+="(time):"+std::to_string(intTimeOld)+"/"+std::to_string(st.st_mtim.tv_sec);
            if(!boSizeMatch) strTmp+="(size):"+std::to_string(intSizeOld)+"/"+std::to_string(st.st_size);
            
            if(strcmp(basename(strFile), strFileChk.c_str())==0) { strTmp+=" (as expected)"; }  // else{ strTmp+="(sync wasn't called)";}
            nMissMatchTimeSize++;
          }else{
            strTmp+="(hash):"+std::string(strHashOld)+" / "+std::string(strHash);       
            nMissMatchHash++;
          }
          strTmp+=", %s\n";
          printf(strTmp.c_str(), iRowCount, strFile);
          printf("\n\n" ANSI_CURSOR_UP(2) ANSI_CURSOR_SAVE);  // Save cursor
        }
        else nOK++;
      }
    }else {
      toc(nHour, nMin, nSec);
      printf(ANSI_CURSOR_RESTORE ANSI_CLEAR_BELOW "Time: %d:%02d:%02d, Done (RowCount: %d, NotFound: %d, MissMatchTimeSize: %d, MissMatchHash: %d, OK: %d)\n", nHour, nMin, nSec, iRowCount, nNotFound, nMissMatchTimeSize, nMissMatchHash, nOK);
      break;
    }
  }
  return 0;
}


void helpTextExit( int argc, char **argv){
  printf("Help text: see https://emagnusandersson.com/hashbert\n", argv[0]);
  exit(0);
}

class InputParser{
  public:
    InputParser (int &argc, char **argv){
      for (int i=1; i < argc; ++i)
        this->tokens.push_back(std::string(argv[i]));
    }
    /// @author iain
    const std::string& getCmdOption(const std::string &option) const{
      std::vector<std::string>::const_iterator itr;
      itr =  std::find(this->tokens.begin(), this->tokens.end(), option);
      if (itr != this->tokens.end() && ++itr != this->tokens.end()){
        return *itr;
      }
      static const std::string empty_string("");
      return empty_string;
    }
    /// @author iain
    bool cmdOptionExists(const std::string &option) const{
      return std::find(this->tokens.begin(), this->tokens.end(), option)
        != this->tokens.end();
    }
  private:
    std::vector <std::string> tokens;
};

//#include <boost/algorithm/string.hpp>
int main( int argc, char **argv){
  char boCheck=0;
  //std::string strDir=".";
  //std::string strFileChk, strFileTmp, strFileNew;     strFileChk=strFileTmp=strFileNew="hashcodes.txt";
  //std::string strFileChk, strFileNew;     strFileChk=strFileNew="hashcodes.txt";
  
  InputParser input(argc, argv);
  if(argc==1 || input.cmdOptionExists("-h") || input.cmdOptionExists("--help") ){ helpTextExit(argc, argv);   }
  if(strcmp(argv[1],"sync")==0) boCheck=0;
  else if(strcmp(argv[1],"check")==0) boCheck=1;
  else { helpTextExit(argc, argv);   }
  
  const std::string &strFilter = input.getCmdOption("-r");
  if(!strFilter.empty()){
    if(!boCheck) {printf("Filter rules (the -r option) can only be used with \"check\"\n"); exit(0);}
    readFilterFrCommandLine(strFilter);
  }
  //if(input.cmdOptionExists("-F")){ 
    //if(!boCheck) {printf("Filter rules (the -F option) can only be used with \"check\"\n"); exit(0);}
    //const std::string &strFileFilterTmp = input.getCmdOption("-F");
    //std::string strFileFilter=strFileFilterTmp;
    //if(strFileFilterTmp.empty())  strFileFilter=".hashbert_filter";
    //if(readFilterFile(strFileFilter)) {perror("Error in readFilterFile"); return 1;}
  //}
  if(input.cmdOptionExists("-F")){ 
    if(!boCheck) {printf("Filter rules (the -F option) can only be used with \"check\"\n"); exit(0);}
    std::string strFileFilter = input.getCmdOption("-F");
    if(strFileFilter.empty() || strFileFilter[0]=='-')  strFileFilter=".hashbert_filter";
    if(readFilterFile(strFileFilter)) {perror("Error in readFilterFile"); return 1;}
  }
  std::string strFileChk, strFileNew;  strFileChk=strFileNew= input.getCmdOption("-f");
  if(strFileChk.empty()) strFileChk=strFileNew="hashcodes.txt";
   
  std::string strDir = input.getCmdOption("-d");  if(strDir.empty()) strDir=".";
  std::string strStart = input.getCmdOption("--start"); if(strStart.empty()) strStart="0";
  int intStart = std::stoi(strStart);
  


  //strFileTmp.append(".filesonly.tmp");
  strFileNew.append(".new.tmp");
  
  if(boCheck){
    //FILE *fptOld;
    //if(access(strFileChk.c_str(), F_OK)==-1) {perror(""); return 1;}
    //fptOld = fopen(strFileChk.c_str(), "r");
    //check(fptOld);
    //fclose(fptOld);
    
    if(check(strFileChk, intStart)) {perror("Error in check"); return 1;}
  }else{
    int fd;
    char strFileTmp[] = "/tmp/fileXXXXXX";
    //FILE *fptTmp =mkstemp(strFileTmp);
    FILE *fptTmp=tmpfile();
    //FILE *fptTmp = fopen(strFileTmp.c_str(), "w+");

    printf("\n\n\n\n" ANSI_CURSOR_UP(4));  // 4 newlines (makes it scroll if you are on the bottom line), then go up 4 rows
    printf("Reading filenames, modification dates and file sizes from the selected folder.\n" ANSI_CURSOR_SAVE);  // Save cursor at the end
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



