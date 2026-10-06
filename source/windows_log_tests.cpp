// Windows integration checks exercise the same Win32 tailing functions used
// by the shipped app. No game, real chat logs, or audio device is required.
#include "main.cpp"
#include <iostream>
#include <stdexcept>
namespace {
int checks=0;
void expect(bool condition,const char *label) {
    ++checks;
    if(!condition) throw std::runtime_error(label);
}
void writeFixture(const std::wstring &path,const std::string &bytes,bool append=false) {
    HANDLE f=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                         nullptr,append?OPEN_ALWAYS:CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    expect(f!=INVALID_HANDLE_VALUE,"fixture opened");
    if(append) { LARGE_INTEGER zero{}; SetFilePointerEx(f,zero,nullptr,FILE_END); }
    DWORD written=0; bool ok=WriteFile(f,bytes.data(),static_cast<DWORD>(bytes.size()),&written,nullptr) && written==bytes.size();
    FlushFileBuffers(f); CloseHandle(f); expect(ok,"fixture written");
}
std::string chat(const char *speaker,const std::string &message) {
    return std::string("[CHAT WINDOW TEXT] [Sun Oct 04 20:35:17] ")+speaker+": "+message+"\r\n";
}
}
int wmain() {
    std::array<wchar_t,MAX_PATH> temp{}; GetTempPathW(static_cast<DWORD>(temp.size()),temp.data());
    auto folder=join(temp.data(),L"NWNChime-tests-"+std::to_wstring(GetCurrentProcessId()));
    CreateDirectoryW(folder.c_str(),nullptr);
    auto log=join(folder,L"nwclientLog1.txt"), rotated=join(folder,L"nwclientLog2.txt");
    int code=0;
    try {
        config.folder=folder; filters.ownNames={"Khalen"};
        expect(!gate.enabled,"Windows app gate starts OFF");
        writeFixture(log,chat("Mela","Historical message."));
        pollLogs(true); expect(logCount==1 && chatsRead==0,"startup snapshots existing log without playing history");
        setEnabled(true); pollLogs(); expect(chatsRead==0 && !gate.hasPlayed,"turning ON does not replay history");
        writeFixture(log,chat("Mela","New nearby speech."),true); pollLogs();
        expect(chatsRead==1 && gate.hasPlayed,"new unlabelled nearby chat arms notification");
        expect(lastPreview.find(L"New nearby speech.")!=std::wstring::npos,"preview shows new message");
        auto preview=lastPreview;
        writeFixture(log,chat("Khalen","My own speech."),true); pollLogs();
        expect(chatsRead==2 && lastPreview==preview,"own-name filter excludes own message in actual log reader");
        setEnabled(false); writeFixture(log,chat("Mela","Message while OFF."),true); pollLogs();
        expect(chatsRead==0 && !gate.hasPlayed,"OFF skips incoming chat and stays silent");
        setEnabled(true); pollLogs();
        expect(chatsRead==0 && !gate.hasPlayed && lastPreview==preview,"reenabling discards OFF backlog");
        auto split=chat("Mela","Split write.");
        writeFixture(log,split.substr(0,split.size()-1),true); pollLogs();
        expect(chatsRead==0,"partial CRLF does not create premature message");
        writeFixture(log,"\n",true); pollLogs();
        expect(chatsRead==1 && lastPreview.find(L"Split write.")!=std::wstring::npos,"later newline completes partial message");
        writeFixture(log,chat("Mela","After truncate.")); pollLogs();
        expect(chatsRead==2 && lastPreview.find(L"After truncate.")!=std::wstring::npos,"same-file truncation followed from start");
        std::string regrown=chat("Mela","Completely rewritten "+std::string(300,'x'));
        writeFixture(log,regrown); pollLogs();
        expect(chatsRead==3 && lastPreview.find(L"Completely rewritten")!=std::wstring::npos,"truncate then regrow beyond old size detected by anchor");
        writeFixture(rotated,chat("Mela","Second rolling log.")); pollLogs();
        expect(logCount==2 && chatsRead==4 && lastPreview.find(L"Second rolling log.")!=std::wstring::npos,"new rolling log picked up while ON");
        auto replacement=join(folder,L"replacement.txt");
        writeFixture(replacement,chat("Mela","New file identity."));
        expect(MoveFileExW(replacement.c_str(),log.c_str(),MOVEFILE_REPLACE_EXISTING)!=FALSE,"fixture replaced by move");
        pollLogs(); expect(chatsRead==5 && lastPreview.find(L"New file identity.")!=std::wstring::npos,"replacement file identity detected");
        writeFixture(log,"[CHAT WINDOW TEXT] [Sun Oct 04 20:35:18] Mela: Old partial",true);
        setEnabled(false); setEnabled(true); writeFixture(log," finished\r\n"+chat("Mela","Fresh after old partial."),true); pollLogs();
        expect(chatsRead==1 && lastPreview.find(L"Fresh after old partial.")!=std::wstring::npos,"old partial line at ON boundary discarded");
        // A producer keeps its shared-write handle open, as the game does.
        HANDLE writer=CreateFileW(log.c_str(),FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,
                                 nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        expect(writer!=INVALID_HANDLE_VALUE,"simulated game writer remains open");
        auto live=chat("Mela","Shared handle message."); DWORD n=0; WriteFile(writer,live.data(),static_cast<DWORD>(live.size()),&n,nullptr); FlushFileBuffers(writer);
        pollLogs(); expect(chatsRead==2 && lastPreview.find(L"Shared handle message.")!=std::wstring::npos,"tailing works with game write handle open");
        CloseHandle(writer);
        expect(isLogName(L"nwClientLog4.txt") && isLogName(L"nwnclientLog1.txt") && !isLogName(L"ChatBackup.txt"),"only actual client logs selected");
        instance=GetModuleHandleW(nullptr); config.customSound.clear(); loadSound();
        expect(soundAvailable,"built-in sound resource loads in Windows");
        config.volume=65; applyVolume(); size_t start=0,len=0;
        expect(wavInfo(playbackSound,start,len) && len==198450,"volume-adjusted original chime stays valid WAV");
        std::cout<<"PASS: "<<checks<<" Windows log, switch, rotation, shared-handle and embedded-WAV checks.\n";
    } catch(const std::exception &e) { std::cerr<<"FAIL: "<<e.what()<<" after "<<checks<<" checks.\n"; code=1; }
    DeleteFileW(log.c_str()); DeleteFileW(rotated.c_str()); DeleteFileW(join(folder,L"replacement.txt").c_str()); RemoveDirectoryW(folder.c_str());
    return code;
}
