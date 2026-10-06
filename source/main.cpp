#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>
#include <commdlg.h>
#include <mmsystem.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <cwctype>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include "chat_core.hpp"
#include <cstdio>

namespace {
constexpr wchar_t CLASS_NAME[] = L"NWNChatChimeWindow_v1";
enum { ID_TOGGLE=100, ID_BROWSE, ID_NAMES, ID_CONTAINS,
       ID_LOCAL, ID_WHISPER, ID_TELL, ID_PARTY, ID_SHOUT, ID_BACKGROUND,
       ID_TEST, ID_VOLUME, ID_COOLDOWN, ID_SOUND, ID_RESET_SOUND, ID_HELP,
       ID_IGNORE_SYSTEM, ID_HISTORY, ID_HISTORY_SPEAKER, ID_HISTORY_CLEAR, ID_HISTORY_ALL, ID_OPEN=200, ID_EXIT, ID_TRAY_TOGGLE };
constexpr UINT WM_TRAY = WM_APP + 1;
HINSTANCE instance;
HWND window, toggleButton, folderEdit, ownEdit, containsEdit, volumeSlider;
HWND statusText, previewEdit, soundText, hotkeyText, cooldownEdit;
HFONT bodyFont, titleFont, buttonFont;
HBRUSH backgroundBrush;
int dpi=96;
bool ready=false, trayAdded=false, hotkeyRegistered=false;
UINT taskbarCreated;
NOTIFYICONDATAW tray{};
struct Config {
    std::wstring folder, names, contains, customSound;
    bool local=true, whisper=true, tell=true, party=false, shout=false, background=false, ignoreSystem=true;
    int volume=65, cooldown=4;
} config;
std::wstring configFile;
chime::AlertGate gate;
chime::Filters filters;
chime::ChatHistory chatHistory;
HWND historyWindow=nullptr, historySelector=nullptr, historyText=nullptr;
std::vector<std::string> historySpeakers;
bool historyAll=true;
void refreshHistory(bool rebuildNames=false);
void showHistory();
std::vector<unsigned char> originalSound, playbackSound;
bool soundAvailable=false;
uint64_t linesRead=0, chatsRead=0, lastRead=0;
unsigned int logCount=0, unreadableCount=0;
std::wstring lastPreview=L"No matching messages yet. Turn alerts on, then have someone speak nearby.";
std::map<std::wstring, chime::TailCursor> cursors;

int px(int v) { return MulDiv(v,dpi,96); }
std::wstring join(const std::wstring &a,const std::wstring &b) {
    return a.empty() ? b : a + (a.back()==L'\\' || a.back()==L'/' ? L"" : L"\\") + b;
}

std::wstring basename(const std::wstring &p) {
    auto pos=p.find_last_of(L"\\/"); return pos==std::wstring::npos ? p : p.substr(pos+1);
}
bool exists(const std::wstring &p) { return GetFileAttributesW(p.c_str())!=INVALID_FILE_ATTRIBUTES; }
std::wstring knownFolder(REFKNOWNFOLDERID id) {
    PWSTR p=nullptr; std::wstring result;
    if (SUCCEEDED(SHGetKnownFolderPath(id,0,nullptr,&p))) { result=p; CoTaskMemFree(p); }
    return result;
}
std::wstring text(HWND h) {
    int n=GetWindowTextLengthW(h); std::vector<wchar_t> buf(static_cast<size_t>(n)+1);
    GetWindowTextW(h,buf.data(),n+1); return std::wstring(buf.data());
}
std::string utf8(const std::wstring &s) {
    int n=WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),nullptr,0,nullptr,nullptr);
    std::string out(static_cast<size_t>(n),'\0');
    if(n) WideCharToMultiByte(CP_UTF8,0,s.data(),static_cast<int>(s.size()),out.data(),n,nullptr,nullptr);
    return out;
}
std::wstring wide(const std::string &s) {
    int n=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,s.data(),static_cast<int>(s.size()),nullptr,0);
    UINT cp=CP_UTF8; DWORD flags=MB_ERR_INVALID_CHARS;
    if(!n && !s.empty()) { cp=1252; flags=0; n=MultiByteToWideChar(cp,flags,s.data(),static_cast<int>(s.size()),nullptr,0); }
    std::wstring out(static_cast<size_t>(n),L'\0');
    if(n) MultiByteToWideChar(cp,flags,s.data(),static_cast<int>(s.size()),out.data(),n);
    return out;
}
void error(const std::wstring &msg) { MessageBoxW(window,msg.c_str(),L"NWN Chat Chime",MB_OK|MB_ICONWARNING); }
std::string readBytes(const std::wstring &path, bool &ok, DWORD limit=8*1024*1024) {
    ok=false; HANDLE f=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                                  nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(f==INVALID_HANDLE_VALUE) return {};
    LARGE_INTEGER size{};
    if(!GetFileSizeEx(f,&size) || size.QuadPart<0 || size.QuadPart>limit) { CloseHandle(f); return {}; }
    std::string bytes(static_cast<size_t>(size.QuadPart),'\0'); DWORD got=0;
    ok=ReadFile(f,bytes.data(),static_cast<DWORD>(bytes.size()),&got,nullptr) && got==bytes.size();
    CloseHandle(f); if(!ok) bytes.clear(); return bytes;
}
bool checked(int id) { return IsDlgButtonChecked(window,id)==BST_CHECKED; }
void check(int id,bool on) { CheckDlgButton(window,id,on?BST_CHECKED:BST_UNCHECKED); }
void readControls() {
    if(!ready) return;
    config.names=text(ownEdit); config.contains=text(containsEdit);
    config.local=checked(ID_LOCAL); config.whisper=checked(ID_WHISPER); config.tell=checked(ID_TELL);
    config.party=checked(ID_PARTY); config.shout=checked(ID_SHOUT); config.background=checked(ID_BACKGROUND);
    config.ignoreSystem=checked(ID_IGNORE_SYSTEM);
    filters.ignoreSystem=config.ignoreSystem;
    config.volume=static_cast<int>(SendMessageW(volumeSlider,TBM_GETPOS,0,0));
    auto seconds=text(cooldownEdit);
    int n=_wtoi(seconds.c_str()); if(n>=1 && n<=120) config.cooldown=n;
    filters.local=config.local; filters.whispers=config.whisper; filters.tells=config.tell;
    filters.party=config.party; filters.shouts=config.shout;
    filters.ownNames=chime::splitNames(utf8(config.names)); filters.contains=chime::trim(utf8(config.contains));
    gate.cooldownMs=static_cast<uint64_t>(config.cooldown)*1000;
}
void saveConfig(bool refresh=true) {
    if(!ready || configFile.empty()) return;
    if(refresh) readControls();
    auto write=[&](const wchar_t *key,const std::wstring &value) { WritePrivateProfileStringW(L"Chime",key,value.c_str(),configFile.c_str()); };
    write(L"Folder",config.folder); write(L"OwnNames",config.names); write(L"Contains",config.contains);
    write(L"CustomSound",config.customSound); write(L"Volume",std::to_wstring(config.volume));
    write(L"CooldownSeconds",std::to_wstring(config.cooldown));
    write(L"Local",config.local?L"1":L"0"); write(L"Whisper",config.whisper?L"1":L"0");
    write(L"Tell",config.tell?L"1":L"0"); write(L"Party",config.party?L"1":L"0");
    write(L"Shout",config.shout?L"1":L"0"); write(L"BackgroundOnly",config.background?L"1":L"0");
    write(L"IgnoreSystem",config.ignoreSystem?L"1":L"0");
}
void loadConfig() {
    auto appdata=knownFolder(FOLDERID_LocalAppData);
    if(!appdata.empty()) {
        auto folder=join(appdata,L"NWNChatChime"); CreateDirectoryW(folder.c_str(),nullptr);
        configFile=join(folder,L"settings.ini");
        if(!exists(configFile)) {
            HANDLE f=CreateFileW(configFile.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
            if(f!=INVALID_HANDLE_VALUE) { const unsigned char bom[]={0xFF,0xFE}; DWORD n; WriteFile(f,bom,2,&n,nullptr); CloseHandle(f); }
        }
    }
    config.folder=join(join(knownFolder(FOLDERID_Documents),L"Neverwinter Nights"),L"logs");
    auto get=[&](const wchar_t *key,const std::wstring &fallback) {
        std::array<wchar_t,32768> buf{};
        GetPrivateProfileStringW(L"Chime",key,fallback.c_str(),buf.data(),static_cast<DWORD>(buf.size()),configFile.c_str());
        return std::wstring(buf.data());
    };
    config.folder=get(L"Folder",config.folder); config.names=get(L"OwnNames",L"");
    config.contains=get(L"Contains",L""); config.customSound=get(L"CustomSound",L"");
    auto num=[&](const wchar_t *key,int fallback) { return static_cast<int>(GetPrivateProfileIntW(L"Chime",key,fallback,configFile.c_str())); };
    config.volume=std::clamp(num(L"Volume",65),0,100); config.cooldown=std::clamp(num(L"CooldownSeconds",4),1,120);
    config.local=num(L"Local",1)!=0; config.whisper=num(L"Whisper",1)!=0; config.tell=num(L"Tell",1)!=0;
    config.ignoreSystem=num(L"IgnoreSystem",1)!=0;
    config.party=num(L"Party",0)!=0; config.shout=num(L"Shout",0)!=0; config.background=num(L"BackgroundOnly",0)!=0;
}
uint32_t u32(const unsigned char *p) { return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1])<<8) | (static_cast<uint32_t>(p[2])<<16) | (static_cast<uint32_t>(p[3])<<24); }
uint16_t u16(const unsigned char *p) { return static_cast<uint16_t>(p[0] | (p[1]<<8)); }
bool wavInfo(const std::vector<unsigned char> &b,size_t &dataStart,size_t &dataLen) {
    if(b.size()<44 || memcmp(b.data(),"RIFF",4) || memcmp(b.data()+8,"WAVE",4)) return false;
    bool fmt=false, data=false; uint32_t bytesPerSecond=0;
    for(size_t i=12; i+8<=b.size();) {
        uint32_t len=u32(b.data()+i+4); if(len>b.size()-i-8) return false;
        if(!memcmp(b.data()+i,"fmt ",4)) {
            if(len<16 || u16(b.data()+i+8)!=1 || u16(b.data()+i+22)!=16) return false;
            auto channels=u16(b.data()+i+10); auto rate=u32(b.data()+i+12);
            if((channels!=1 && channels!=2) || rate<8000 || rate>192000) return false;
            bytesPerSecond=u32(b.data()+i+16); fmt=true;
        } else if(!memcmp(b.data()+i,"data",4)) { dataStart=i+8; dataLen=len; data=true; }
        i+=8+len+(len&1);
    }
    return fmt && data && dataLen>0 && !(dataLen&1) && bytesPerSecond && dataLen<=static_cast<uint64_t>(bytesPerSecond)*10;
}
bool loadSound() {
    originalSound.clear();
    if(!config.customSound.empty()) {
        bool ok; auto bytes=readBytes(config.customSound,ok,5*1024*1024);
        if(ok) originalSound.assign(bytes.begin(),bytes.end());
        size_t start=0,len=0;
        if(!wavInfo(originalSound,start,len)) originalSound.clear();
    }
    bool customLoaded=!originalSound.empty();
    if(originalSound.empty()) {
        auto res=FindResourceW(instance,MAKEINTRESOURCEW(301),RT_RCDATA);
        if(res) {
            auto loaded=LoadResource(instance,res); auto p=static_cast<unsigned char*>(LockResource(loaded));
            auto n=SizeofResource(instance,res); if(p && n) originalSound.assign(p,p+n);
        }
    }
    size_t start=0,len=0;
    soundAvailable=wavInfo(originalSound,start,len);
    if(soundText) SetWindowTextW(soundText,customLoaded?basename(config.customSound).c_str():L"Fantasy chime (built in)");
    return customLoaded;
}
void applyVolume() {
    PlaySoundW(nullptr,nullptr,0); playbackSound=originalSound;
    size_t start=0,len=0; soundAvailable=wavInfo(playbackSound,start,len);
    if(!soundAvailable) return;
    for(size_t i=start;i+1<start+len;i+=2) {
        int16_t sample=static_cast<int16_t>(u16(playbackSound.data()+i));
        sample=static_cast<int16_t>(sample*config.volume/100);
        auto value=static_cast<uint16_t>(sample);
        playbackSound[i]=static_cast<unsigned char>(value&255); playbackSound[i+1]=static_cast<unsigned char>(value>>8);
    }
}
bool playSound(bool manual=false) {
    if(!manual && !gate.enabled) return false;
    if(!soundAvailable) { if(manual) error(L"The chime could not be loaded. Use Reset sound or choose a 16-bit PCM WAV."); return false; }
    if(config.volume==0) { if(manual) error(L"The chime volume is set to zero. Move the volume slider to hear it."); return false; }
    bool ok=PlaySoundW(reinterpret_cast<LPCWSTR>(playbackSound.data()),nullptr,SND_MEMORY|SND_ASYNC|SND_NODEFAULT)!=FALSE;
    if(!ok && manual) error(L"Windows could not play the chime. Check your audio output and volume mixer.");
    return ok;
}
bool nwnForeground() {
    DWORD pid=0; GetWindowThreadProcessId(GetForegroundWindow(),&pid);
    HANDLE process=OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION,FALSE,pid);
    if(!process) return false;
    std::array<wchar_t,32768> path{}; DWORD n=static_cast<DWORD>(path.size());
    bool result=false;
    if(QueryFullProcessImageNameW(process,0,path.data(),&n)) {
        auto name=basename(std::wstring(path.data(),n));
        std::transform(name.begin(),name.end(),name.begin(),::towlower);
        result=name==L"nwmain.exe" || name==L"nwnmain.exe";
    }
    CloseHandle(process); return result;
}
std::vector<std::string> selectedHistorySpeakers() {
    std::vector<std::string> selected;
    if(!historySelector) return selected;
    for(size_t i=0;i<historySpeakers.size();++i)
        if(SendMessageW(historySelector,LB_GETSEL,i,0)>0) selected.push_back(historySpeakers[i]);
    return selected;
}
void refreshHistory(bool rebuildNames) {
    if(!historyWindow) return;
    auto selected=selectedHistorySpeakers();
    if(rebuildNames) {
        auto names=chatHistory.speakers();
        if(names!=historySpeakers) {
            historySpeakers=names;
            SendMessageW(historySelector,LB_RESETCONTENT,0,0);
            for(size_t i=0;i<names.size();++i) {
                auto name=wide(names[i]);
                SendMessageW(historySelector,LB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));
                bool chosen=historyAll || std::any_of(selected.begin(),selected.end(),[&](const std::string &n){return chime::lower(n)==chime::lower(names[i]);});
                SendMessageW(historySelector,LB_SETSEL,chosen,i);
            }
            selected=selectedHistorySpeakers();
        }
    }
    auto transcript=wide(chatHistory.transcriptFor(selected,historyAll));
    if(!historyAll && selected.empty()) transcript=L"Select one or more characters on the left.";
    if(text(historyText)==transcript) return;
    auto first=SendMessageW(historyText,EM_GETFIRSTVISIBLELINE,0,0);
    auto count=SendMessageW(historyText,EM_GETLINECOUNT,0,0);
    RECT rect{}; GetClientRect(historyText,&rect);
    bool atBottom=first+(rect.bottom-rect.top)/px(19)>=count-2;
    DWORD start=0,end=0; SendMessageW(historyText,EM_GETSEL,reinterpret_cast<WPARAM>(&start),reinterpret_cast<LPARAM>(&end));
    SetWindowTextW(historyText,transcript.c_str());
    if(atBottom && start==end) {
        SendMessageW(historyText,EM_SETSEL,transcript.size(),transcript.size());
        SendMessageW(historyText,EM_SCROLLCARET,0,0);
    } else {
        SendMessageW(historyText,EM_SETSEL,start,end);
        SendMessageW(historyText,EM_LINESCROLL,0,first);
    }
}
LRESULT CALLBACK historyProc(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
    switch(msg) {
    case WM_CREATE: {
        historyWindow=h;
        auto label=CreateWindowW(L"STATIC",L"Click names to select or deselect several characters.",WS_CHILD|WS_VISIBLE,px(16),px(18),px(410),px(24),h,nullptr,instance,nullptr);
        historySelector=CreateWindowExW(WS_EX_CLIENTEDGE,L"LISTBOX",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|LBS_MULTIPLESEL|LBS_NOTIFY|LBS_NOINTEGRALHEIGHT|WS_VSCROLL|WS_HSCROLL,px(16),px(56),px(220),px(340),h,reinterpret_cast<HMENU>(ID_HISTORY_SPEAKER),instance,nullptr);
        SendMessageW(historySelector,LB_SETHORIZONTALEXTENT,px(600),0);
        auto all=CreateWindowW(L"BUTTON",L"All characters",WS_CHILD|WS_VISIBLE|WS_TABSTOP,px(440),px(14),px(130),px(28),h,reinterpret_cast<HMENU>(ID_HISTORY_ALL),instance,nullptr);
        auto clear=CreateWindowW(L"BUTTON",L"Clear history",WS_CHILD|WS_VISIBLE|WS_TABSTOP,px(584),px(14),px(130),px(28),h,reinterpret_cast<HMENU>(ID_HISTORY_CLEAR),instance,nullptr);
        historyText=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",L"",WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_MULTILINE|ES_READONLY|WS_VSCROLL,px(252),px(56),px(462),px(340),h,nullptr,instance,nullptr);
        SendMessageW(historyText,EM_SETLIMITTEXT,0x7ffffffe,0);
        for(auto child:{label,historySelector,all,clear,historyText}) SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(bodyFont),TRUE);
        historyAll=true;
        historySpeakers.clear(); refreshHistory(true); return 0;
    }
    case WM_SIZE:
        if(historyText) {
            int height=std::max(px(60),static_cast<int>(HIWORD(lp))-px(72));
            MoveWindow(historySelector,px(16),px(56),px(220),height,TRUE);
            MoveWindow(historyText,px(252),px(56),std::max(px(100),static_cast<int>(LOWORD(lp))-px(268)),height,TRUE);
        }
        return 0;
    case WM_GETMINMAXINFO: {
        auto limits=reinterpret_cast<MINMAXINFO*>(lp); limits->ptMinTrackSize={px(760),px(300)}; return 0;
    }
    case WM_COMMAND:
        if(LOWORD(wp)==ID_HISTORY_SPEAKER && HIWORD(wp)==LBN_SELCHANGE) {
            historyAll=false; SetWindowTextW(historyText,L""); refreshHistory(); return 0;
        }
        if(LOWORD(wp)==ID_HISTORY_ALL) {
            historyAll=true; SendMessageW(historySelector,LB_SETSEL,TRUE,-1);
            SetWindowTextW(historyText,L""); refreshHistory(); return 0;
        }
        if(LOWORD(wp)==ID_HISTORY_CLEAR && MessageBoxW(h,L"Clear the chat history collected during this run?",L"Clear chat history",MB_YESNO|MB_ICONQUESTION)==IDYES) {
            chatHistory.entries.clear(); refreshHistory(true); return 0;
        }
        break;
    case WM_CLOSE: ShowWindow(h,SW_HIDE); return 0;
    case WM_DESTROY: historyWindow=nullptr; historySelector=nullptr; historyText=nullptr; historySpeakers.clear(); return 0;
    }
    return DefWindowProcW(h,msg,wp,lp);
}
void showHistory() {
    if(!historyWindow) {
        WNDCLASSEXW cls{}; cls.cbSize=sizeof(cls); cls.hInstance=instance; cls.lpfnWndProc=historyProc;
        cls.lpszClassName=L"NWNChatChimeHistory"; cls.hCursor=LoadCursorW(nullptr,IDC_ARROW); cls.hbrBackground=backgroundBrush;
        RegisterClassExW(&cls);
        historyWindow=CreateWindowExW(0,cls.lpszClassName,L"Chat history by character",WS_OVERLAPPEDWINDOW,CW_USEDEFAULT,CW_USEDEFAULT,px(760),px(480),window,nullptr,instance,nullptr);
    }
    ShowWindow(historyWindow,SW_SHOW); SetForegroundWindow(historyWindow);
}
void handleLine(const std::string &rawLine) {
    // Older clients may log Windows-1252; modern clients may log UTF-8.
    // Normalize before comparing character names and displaying previews.
    auto line=utf8(wide(rawLine));
    ++linesRead;
    chime::Message message;
    if(!chime::parseMessage(line,message)) return;
    ++chatsRead;
    if(!(config.ignoreSystem && message.system)) {
        SYSTEMTIME now{}; GetLocalTime(&now);
        char time[16]; std::snprintf(time,sizeof(time),"%02u:%02u:%02u",now.wHour,now.wMinute,now.wSecond);
        static const std::regex clock(R"(\b\d{1,2}:\d{2}:\d{2}\b)");
        std::smatch found;
        std::string stamp=time;
        auto colon=line.find(']');
        auto prefix=colon==std::string::npos?std::string{}:line.substr(0,colon+1);
        if(std::regex_search(prefix,found,clock)) stamp=found.str();
        chatHistory.add(message,stamp);
        refreshHistory(true);
    }
    if(!filters.matches(message)) return;
    lastPreview=wide(std::string(chime::channelName(message.channel))+" | "+message.speaker+": "+message.text);
    if(lastPreview.size()>1200) lastPreview.resize(1200);
    SetWindowTextW(previewEdit,lastPreview.c_str());
    bool suppress=config.background && nwnForeground();
    if(gate.accept(line,GetTickCount64(),suppress)) playSound();
}
bool seek(HANDLE file,uint64_t offset) {
    LARGE_INTEGER pos{}; pos.QuadPart=static_cast<LONGLONG>(offset);
    return SetFilePointerEx(file,pos,nullptr,FILE_BEGIN)!=FALSE;
}
std::string anchor(HANDLE file,uint64_t end) {
    DWORD size=static_cast<DWORD>(std::min<uint64_t>(end,64));
    std::string bytes(size,'\0'); DWORD got=0;
    if(!seek(file,end-size) || !ReadFile(file,bytes.data(),size,&got,nullptr)) return {};
    bytes.resize(got); return bytes;
}
bool isLogName(const std::wstring &name) {
    static const std::wregex pattern(LR"(^(nw|nwn)clientlog[0-9]*\.txt$)",std::regex::icase);
    return std::regex_match(name,pattern);
}
void pollLogs(bool snapshot=false) {
    logCount=0; unreadableCount=0;
    WIN32_FIND_DATAW data{};
    HANDLE list=FindFirstFileW(join(config.folder,L"*.txt").c_str(),&data);
    if(list==INVALID_HANDLE_VALUE) return;
    do {
        if((data.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY) || !isLogName(data.cFileName)) continue;
        auto path=join(config.folder,data.cFileName);
        HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                                nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        if(file==INVALID_HANDLE_VALUE) { ++unreadableCount; continue; }
        BY_HANDLE_FILE_INFORMATION info{};
        if(!GetFileInformationByHandle(file,&info)) { CloseHandle(file); ++unreadableCount; continue; }
        ++logCount;
        uint64_t size=(static_cast<uint64_t>(info.nFileSizeHigh)<<32)|info.nFileSizeLow;
        uint64_t id=(static_cast<uint64_t>(info.nFileIndexHigh)<<32)|info.nFileIndexLow;
        uint64_t created=(static_cast<uint64_t>(info.ftCreationTime.dwHighDateTime)<<32)|info.ftCreationTime.dwLowDateTime;
        auto &cursor=cursors[path];
        bool skip=snapshot || !gate.enabled;
        auto atOldOffset=(!skip && cursor.initialized && cursor.offset<=size)?anchor(file,cursor.offset):std::string{};
        auto atEnd=skip?anchor(file,size):std::string{};
        cursor.prepare(size,id,created,info.dwVolumeSerialNumber,atOldOffset,atEnd,skip);
        if(!skip) {
            if(size>cursor.offset) {
                DWORD wanted=static_cast<DWORD>(std::min<uint64_t>(size-cursor.offset,512*1024));
                std::string bytes(wanted,'\0'); DWORD got=0;
                if(seek(file,cursor.offset) && ReadFile(file,bytes.data(),wanted,&got,nullptr)) {
                    bytes.resize(got);
                    if(got) {
                        lastRead=GetTickCount64();
                        cursor.consume(bytes,anchor(file,cursor.offset+got),handleLine);
                    }
                }
            }
        }
        CloseHandle(file);
    } while(FindNextFileW(list,&data));
    FindClose(list);
}
void updateStatus() {
    std::wstring status;
    if(!exists(config.folder)) status=L"Logs folder not found. Run NWN once, or choose your logs folder.";
    else if(!logCount) status=unreadableCount?L"Windows could not read the chat log. Check the folder's permissions.":L"Waiting for NWN to create nwclientLog1.txt. Enable chat logging in NWN Options if needed.";
    else if(!gate.enabled) status=L"OFF. No automatic sounds. Old messages will be skipped when you turn it on.";
    else {
        status=L"ON. Watching "+std::to_wstring(logCount)+L" log file(s). "+std::to_wstring(chatsRead)+L" chat line(s) read.";
        if(lastRead) status+=L" Last update "+std::to_wstring((GetTickCount64()-lastRead)/1000)+L"s ago.";
        else status+=L" Waiting for new chat.";
    }
    SetWindowTextW(statusText,status.c_str());
    SetWindowTextW(window,gate.enabled?L"NWN Chat Chime - ON":L"NWN Chat Chime - OFF");
}
void updateTray() {
    tray.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(gate.enabled?101:102));
    wcscpy_s(tray.szTip,gate.enabled?L"NWN Chat Chime: ON (Ctrl+Alt+F8)":L"NWN Chat Chime: OFF (Ctrl+Alt+F8)");
    if(trayAdded) Shell_NotifyIconW(NIM_MODIFY,&tray);
}
void setEnabled(bool on) {
    readControls();
    // Take a fresh end-of-file snapshot at both transitions. The OFF interval
    // can never leave messages queued for the next ON interval.
    pollLogs(true); gate.setEnabled(on);
    if(!on) PlaySoundW(nullptr,nullptr,0);
    linesRead=0; chatsRead=0; lastRead=0;
    SetWindowTextW(toggleButton,on?L"ON  -  Click to turn alerts off":L"OFF  -  Click to turn alerts on");
    InvalidateRect(toggleButton,nullptr,TRUE); updateTray(); updateStatus(); saveConfig();
}
void showWindow() { ShowWindow(window,SW_RESTORE); SetForegroundWindow(window); }
void trayMenu() {
    HMENU menu=CreatePopupMenu();
    AppendMenuW(menu,MF_STRING,ID_OPEN,L"Open NWN Chat Chime");
    AppendMenuW(menu,MF_STRING,ID_TRAY_TOGGLE,gate.enabled?L"Turn alerts OFF":L"Turn alerts ON");
    AppendMenuW(menu,MF_STRING,ID_TEST,L"Test chime"); AppendMenuW(menu,MF_SEPARATOR,0,nullptr);
    AppendMenuW(menu,MF_STRING,ID_EXIT,L"Quit (stop all alerts)");
    POINT p{}; GetCursorPos(&p); SetForegroundWindow(window);
    TrackPopupMenu(menu,TPM_RIGHTBUTTON,p.x,p.y,0,window,nullptr); DestroyMenu(menu);
    PostMessageW(window,WM_NULL,0,0);
}
void help() {
    MessageBoxW(window,
        L"1. Select your Neverwinter Nights\\logs folder if it was not found.\n\n"
        L"2. In NWN, open Options and search/filter for log. Enable Game Log Chat Text (game.log.chat.text.enabled). Leave Game Log Chat All (game.log.chat.all.enabled) off to avoid duplicate chat and combat output. Apply/save your changes.\n\n"
        L"3. Enter your own character's exact displayed name so your messages are ignored. Separate multiple names with semicolons.\n\n"
        L"4. Click Test chime, then turn alerts ON. Ask another player to speak nearby.\n\n"
        L"Local speech and whispers indicate chat heard nearby. Tells are private messages. The log cannot show a silent arrival or prove a message was addressed to you. NPC dialogue may also match.\n\n"
        L"Ctrl+Alt+F8 toggles alerts. You can also use the tray icon. Minimize keeps the app running; closing quits. It always starts OFF.\n\n"
        L"The app reads local log files and plays audio. It does not connect to a server, inject into NWN, or send any game input.",
        L"NWN Chat Chime - logging and setup help",MB_OK|MB_ICONINFORMATION);
}
int CALLBACK browseCallback(HWND h,UINT msg,LPARAM,LPARAM value) {
    if(msg==BFFM_INITIALIZED) SendMessageW(h,BFFM_SETSELECTIONW,TRUE,value);
    return 0;
}
void browseFolder() {
    BROWSEINFOW browse{}; browse.hwndOwner=window;
    browse.lpszTitle=L"Choose the Neverwinter Nights logs folder";
    browse.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE; browse.lpfn=browseCallback;
    browse.lParam=reinterpret_cast<LPARAM>(config.folder.c_str());
    auto list=SHBrowseForFolderW(&browse); if(!list) return;
    std::array<wchar_t,MAX_PATH> path{};
    if(SHGetPathFromIDListW(list,path.data())) {
        config.folder=path.data(); SetWindowTextW(folderEdit,config.folder.c_str());
        cursors.clear(); pollLogs(true); lastRead=0; linesRead=0; chatsRead=0;
        updateStatus(); saveConfig();
    }
    CoTaskMemFree(list);
}
void chooseSound() {
    std::array<wchar_t,32768> path{}; OPENFILENAMEW choose{};
    choose.lStructSize=sizeof(choose); choose.hwndOwner=window;
    choose.lpstrFilter=L"16-bit PCM WAV sound\0*.wav\0"; choose.lpstrFile=path.data();
    choose.nMaxFile=static_cast<DWORD>(path.size()); choose.lpstrTitle=L"Choose a short message tone";
    choose.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR;
    if(!GetOpenFileNameW(&choose)) return;
    bool ok; auto bytes=readBytes(path.data(),ok,5*1024*1024);
    std::vector<unsigned char> wav(bytes.begin(),bytes.end()); size_t start=0,len=0;
    if(!ok || !wavInfo(wav,start,len)) { error(L"Choose a 16-bit PCM WAV, mono or stereo, at most 10 seconds long."); return; }
    PlaySoundW(nullptr,nullptr,0); config.customSound=path.data(); loadSound(); applyVolume(); saveConfig(); playSound(true);
}
HWND control(const wchar_t *cls,const wchar_t *caption,DWORD style,int x,int y,int w,int h,int id=0,DWORD ex=0) {
    auto child=CreateWindowExW(ex,cls,caption,WS_CHILD|WS_VISIBLE|style,px(x),px(y),px(w),px(h),window,reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),instance,nullptr);
    SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(bodyFont),TRUE); return child;
}
HWND label(const wchar_t *caption,int x,int y,int w,int h=22) { return control(L"STATIC",caption,SS_LEFT,x,y,w,h); }
void createControls() {
    auto title=label(L"NWN Chat Chime",24,20,690,38); SendMessageW(title,WM_SETFONT,reinterpret_cast<WPARAM>(titleFont),TRUE);
    label(L"A small bell for nearby conversations.",24,60,690);
    toggleButton=control(L"BUTTON",L"OFF  -  Click to turn alerts on",BS_OWNERDRAW|WS_TABSTOP,24,96,690,56,ID_TOGGLE);
    SendMessageW(toggleButton,WM_SETFONT,reinterpret_cast<WPARAM>(buttonFont),TRUE);
    hotkeyText=label(L"Toggle any time: Ctrl+Alt+F8. Starts OFF each time you open it.",24,162,690);
    label(L"NWN logs folder",24,196,220);
    folderEdit=control(L"EDIT",config.folder.c_str(),ES_AUTOHSCROLL|ES_READONLY,24,222,582,28,0,WS_EX_CLIENTEDGE);
    control(L"BUTTON",L"Browse...",WS_TABSTOP,616,220,98,30,ID_BROWSE);
    label(L"Ignore your own character (exact full name; separate names with ;)",24,264,690);
    ownEdit=control(L"EDIT",config.names.c_str(),WS_TABSTOP|ES_AUTOHSCROLL,24,291,690,28,ID_NAMES,WS_EX_CLIENTEDGE);
    SendMessageW(ownEdit,EM_SETCUEBANNER,TRUE,reinterpret_cast<LPARAM>(L"Example: Khalen"));
    label(L"Alert for",24,333,90);
    control(L"BUTTON",L"Local / emotes",BS_AUTOCHECKBOX|WS_TABSTOP,115,331,150,26,ID_LOCAL);
    control(L"BUTTON",L"Whispers",BS_AUTOCHECKBOX|WS_TABSTOP,270,331,107,26,ID_WHISPER);
    control(L"BUTTON",L"Tells",BS_AUTOCHECKBOX|WS_TABSTOP,385,331,76,26,ID_TELL);
    control(L"BUTTON",L"Party",BS_AUTOCHECKBOX|WS_TABSTOP,472,331,80,26,ID_PARTY);
    control(L"BUTTON",L"Shouts",BS_AUTOCHECKBOX|WS_TABSTOP,567,331,96,26,ID_SHOUT);
    check(ID_LOCAL,config.local); check(ID_WHISPER,config.whisper); check(ID_TELL,config.tell);
    check(ID_PARTY,config.party); check(ID_SHOUT,config.shout);
    control(L"BUTTON",L"Only alert when NWN is in the background",BS_AUTOCHECKBOX|WS_TABSTOP,24,367,415,26,ID_BACKGROUND);
    check(ID_BACKGROUND,config.background);
    control(L"BUTTON",L"Ignore system messages",BS_AUTOCHECKBOX|WS_TABSTOP,445,367,269,26,ID_IGNORE_SYSTEM);
    check(ID_IGNORE_SYSTEM,config.ignoreSystem);
    label(L"Only if text contains (optional)",24,407,257);
    containsEdit=control(L"EDIT",config.contains.c_str(),WS_TABSTOP|ES_AUTOHSCROLL,285,402,429,28,ID_CONTAINS,WS_EX_CLIENTEDGE);
    label(L"Sound",24,452,80); soundText=label(L"Fantasy chime (built in)",102,452,235);
    control(L"BUTTON",L"Test chime",WS_TABSTOP,342,446,112,30,ID_TEST);
    control(L"BUTTON",L"Choose WAV...",WS_TABSTOP,464,446,125,30,ID_SOUND);
    control(L"BUTTON",L"Reset sound",WS_TABSTOP,599,446,115,30,ID_RESET_SOUND);
    label(L"Volume",24,496,70);
    volumeSlider=control(TRACKBAR_CLASSW,L"",TBS_AUTOTICKS|WS_TABSTOP,95,485,237,42,ID_VOLUME);
    SendMessageW(volumeSlider,TBM_SETRANGE,TRUE,MAKELONG(0,100));
    SendMessageW(volumeSlider,TBM_SETPOS,TRUE,config.volume); SendMessageW(volumeSlider,TBM_SETTICFREQ,25,0);
    label(L"Wait at least",352,496,104);
    cooldownEdit=control(L"EDIT",std::to_wstring(config.cooldown).c_str(),WS_TABSTOP|ES_NUMBER|ES_CENTER,458,491,50,27,ID_COOLDOWN,WS_EX_CLIENTEDGE);
    SendMessageW(cooldownEdit,EM_SETLIMITTEXT,3,0); label(L"seconds between chimes (1-120)",520,496,200);
    statusText=label(L"",24,535,690,38);
    label(L"Last matching message",24,579,690);
    previewEdit=control(L"EDIT",lastPreview.c_str(),ES_MULTILINE|ES_READONLY|WS_VSCROLL,24,605,690,64,0,WS_EX_CLIENTEDGE);
    control(L"BUTTON",L"Chat history",WS_TABSTOP,24,680,140,32,ID_HISTORY);
    label(L"History collects new chat while ON. Closing quits.",180,686,415,30);
    control(L"BUTTON",L"Setup help",WS_TABSTOP,606,680,108,32,ID_HELP);
    ready=true; readControls(); loadSound(); applyVolume(); pollLogs(true); updateStatus();
}
LRESULT CALLBACK windowProc(HWND h,UINT msg,WPARAM wp,LPARAM lp) {
    if(taskbarCreated && msg==taskbarCreated) { trayAdded=Shell_NotifyIconW(NIM_ADD,&tray)!=FALSE; updateTray(); return 0; }
    switch(msg) {
    case WM_CREATE:
        window=h; createControls(); return 0;
    case WM_COMMAND: {
        int id=LOWORD(wp), notification=HIWORD(wp);
        switch(id) {
        case ID_TOGGLE: case ID_TRAY_TOGGLE: setEnabled(!gate.enabled); return 0;
        case ID_BROWSE: browseFolder(); return 0;
        case ID_TEST: readControls(); applyVolume(); playSound(true); return 0;
        case ID_SOUND: chooseSound(); return 0;
        case ID_RESET_SOUND: config.customSound.clear(); loadSound(); applyVolume(); saveConfig(); return 0;
        case ID_HELP: help(); return 0;
        case ID_HISTORY: showHistory(); return 0;
        case ID_OPEN: showWindow(); return 0;
        case ID_EXIT: saveConfig(); DestroyWindow(h); return 0;
        default:
            if(ready && id==ID_COOLDOWN && notification==EN_KILLFOCUS) {
                config.cooldown=std::clamp(_wtoi(text(cooldownEdit).c_str()),1,120);
                SetWindowTextW(cooldownEdit,std::to_wstring(config.cooldown).c_str());
            }
            if(ready && (notification==BN_CLICKED || notification==EN_CHANGE)) { readControls(); SetTimer(h,2,500,nullptr); }
            return 0;
        }
    }
    case WM_HSCROLL:
        if(reinterpret_cast<HWND>(lp)==volumeSlider) { readControls(); applyVolume(); SetTimer(h,2,500,nullptr); } return 0;
    case WM_HOTKEY: if(wp==1) setEnabled(!gate.enabled); return 0;
    case WM_TIMER:
        if(wp==1) { pollLogs(); updateStatus(); }
        if(wp==2) { KillTimer(h,2); saveConfig(); } return 0;
    case WM_TRAY:
        if(lp==WM_LBUTTONDBLCLK) showWindow();
        else if(lp==WM_RBUTTONUP || lp==WM_CONTEXTMENU) trayMenu();
        return 0;
    case WM_SIZE:
        if(wp==SIZE_MINIMIZED && trayAdded) ShowWindow(h,SW_HIDE);
        return 0;
    case WM_DRAWITEM: {
        auto draw=reinterpret_cast<DRAWITEMSTRUCT*>(lp); if(draw->CtlID!=ID_TOGGLE) break;
        auto color=gate.enabled?RGB(34,103,72):RGB(57,51,69);
        if(draw->itemState&ODS_SELECTED) color=gate.enabled?RGB(23,79,54):RGB(41,36,49);
        HBRUSH brush=CreateSolidBrush(color); FillRect(draw->hDC,&draw->rcItem,brush); DeleteObject(brush);
        SetBkMode(draw->hDC,TRANSPARENT); SetTextColor(draw->hDC,RGB(255,255,255));
        SelectObject(draw->hDC,buttonFont); auto caption=text(toggleButton); RECT rect=draw->rcItem;
        DrawTextW(draw->hDC,caption.c_str(),-1,&rect,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        if(draw->itemState&ODS_FOCUS) { InflateRect(&rect,-px(4),-px(4)); DrawFocusRect(draw->hDC,&rect); }
        return TRUE;
    }
    case WM_CTLCOLORSTATIC: {
        auto dc=reinterpret_cast<HDC>(wp);
        SetTextColor(dc,RGB(48,43,57)); SetBkColor(dc,RGB(248,246,242));
        return reinterpret_cast<LRESULT>(backgroundBrush);
    }
    case WM_ERASEBKGND: {
        RECT rect{}; GetClientRect(h,&rect); FillRect(reinterpret_cast<HDC>(wp),&rect,backgroundBrush); return TRUE;
    }
    case WM_CLOSE: saveConfig(); DestroyWindow(h); return 0;
    case WM_DESTROY:
        gate.setEnabled(false); PlaySoundW(nullptr,nullptr,0); saveConfig(false);
        if(hotkeyRegistered) UnregisterHotKey(h,1);
        if(trayAdded) Shell_NotifyIconW(NIM_DELETE,&tray);
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(h,msg,wp,lp);
}
} // namespace

int WINAPI wWinMain(HINSTANCE app,HINSTANCE,LPWSTR,int show) {
    HANDLE mutex=CreateMutexW(nullptr,FALSE,L"Local\\NWNChatChime-v1");
    if(mutex && GetLastError()==ERROR_ALREADY_EXISTS) {
        auto other=FindWindowW(CLASS_NAME,nullptr); if(other) { ShowWindow(other,SW_RESTORE); SetForegroundWindow(other); }
        CloseHandle(mutex); return 0;
    }
    instance=app; OleInitialize(nullptr); SetProcessDPIAware();
    HDC dc=GetDC(nullptr); dpi=GetDeviceCaps(dc,LOGPIXELSX); ReleaseDC(nullptr,dc);
    RECT desktop{}; SystemParametersInfoW(SPI_GETWORKAREA,0,&desktop,0);
    // Fit the entire window on smaller screens and at high Windows scaling.
    dpi=std::min(dpi,std::max(72,static_cast<int>((desktop.bottom-desktop.top-50)*96/738)));
    dpi=std::min(dpi,std::max(72,static_cast<int>((desktop.right-desktop.left-30)*96/738)));
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_BAR_CLASSES|ICC_STANDARD_CLASSES}; InitCommonControlsEx(&controls);
    bodyFont=CreateFontW(-px(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    titleFont=CreateFontW(-px(27),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    buttonFont=CreateFontW(-px(19),0,0,0,FW_SEMIBOLD,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
    backgroundBrush=CreateSolidBrush(RGB(248,246,242)); loadConfig();
    WNDCLASSEXW cls{}; cls.cbSize=sizeof(cls); cls.hInstance=instance; cls.lpfnWndProc=windowProc;
    cls.lpszClassName=CLASS_NAME; cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);
    cls.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(101)); cls.hIconSm=cls.hIcon;
    cls.hbrBackground=backgroundBrush; RegisterClassExW(&cls);
    DWORD style=WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX;
    RECT rect{0,0,px(738),px(738)}; AdjustWindowRectEx(&rect,style,FALSE,0);
    window=CreateWindowExW(0,CLASS_NAME,L"NWN Chat Chime - OFF",style,CW_USEDEFAULT,CW_USEDEFAULT,
                           rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,instance,nullptr);
    if(!window) { MessageBoxW(nullptr,L"Windows could not open NWN Chat Chime.",L"NWN Chat Chime",MB_OK|MB_ICONERROR); return 1; }
    tray.cbSize=sizeof(tray); tray.hWnd=window; tray.uID=1;
    tray.uFlags=NIF_ICON|NIF_MESSAGE|NIF_TIP; tray.uCallbackMessage=WM_TRAY;
    tray.hIcon=LoadIconW(instance,MAKEINTRESOURCEW(102)); wcscpy_s(tray.szTip,L"NWN Chat Chime: OFF (Ctrl+Alt+F8)");
    trayAdded=Shell_NotifyIconW(NIM_ADD,&tray)!=FALSE;
    taskbarCreated=RegisterWindowMessageW(L"TaskbarCreated");
    hotkeyRegistered=RegisterHotKey(window,1,MOD_CONTROL|MOD_ALT|MOD_NOREPEAT,VK_F8)!=FALSE;
    if(!hotkeyRegistered) SetWindowTextW(hotkeyText,L"Ctrl+Alt+F8 is in use. Toggle alerts with the button or tray menu.");
    SetTimer(window,1,500,nullptr); ShowWindow(window,show); UpdateWindow(window);
    MSG msg{};
    while(GetMessageW(&msg,nullptr,0,0)>0) {
        if(!(historyWindow && IsDialogMessageW(historyWindow,&msg)) && !IsDialogMessageW(window,&msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    DeleteObject(bodyFont); DeleteObject(titleFont); DeleteObject(buttonFont); DeleteObject(backgroundBrush);
    if(mutex) CloseHandle(mutex);
    OleUninitialize();
    return static_cast<int>(msg.wParam);
}
