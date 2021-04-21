
#if !defined(CMD_OPTION__H)
#define CMD_OPTION__H

#include "popt.h"
#include "resip/stack/Uri.hxx"
#include "rutil/Data.hxx"
#include <string>
#include <list>
#include <memory>

class poptString
{
public:
    explicit poptString(const char* str = 0) : mStr(0) {
        if (str)
        {
            size_t len = strlen(str) + 1;
            mStr = (char*)malloc(sizeof(char) * strlen(str));
            memcpy(mStr, str, len);
        }
    }
    ~poptString() {
        if (mStr) {
            free(mStr);
        }
    }
    operator const char*() const { return mStr; }
    char** operator&() { return &mStr; }
    operator bool() const { return mStr ? true : false; }
private:
    poptString(const poptString& rhs) = delete;
    poptString operator=(const poptString& rhs) = delete;
    char* mStr;
};

class SimpleSBC;
class Cmd
{
public:
    bool toUri(const resip::Data& input, resip::Uri& uri, resip::Data& errmsg, const resip::Data& description = resip::Data::Empty);
    enum Type
    {
        Unknown,
        Reg,
        Call,
        Show,
        Exit,
        Help,
        MaxType,
    };
    static const resip::Data& getCmdName(const Type& type);
    static Type getCmdType(const char* cmdName);
    
    Cmd(Type type, bool showUsage = false);
    Cmd(int argc, const char** argv, Type type, SimpleSBC* sbc);
    virtual ~Cmd();
    Type getCommandType() const;
    const char* getCommandName() const;
    virtual bool run() = 0;
    const char* getLastErr() const { return mErrors.c_str(); }

protected:
    bool parseAndExec(const struct poptOption* table);
    virtual const char* getReplaceHelpText() { return 0; }
    virtual bool processOneOption(poptContext ctx, int ret) { return true; }
    virtual bool processNonOptionArgs(poptContext ctx) { return true; }
    void setLastErr(const char* desc, const char* arg = NULL);
    static const struct poptOption getHelpTable();

    int             mArgc;
    const char**    mArgv;
    Type            mType;
    resip::Data     mErrors;
    SimpleSBC*      mSbc;
};


class CmdRunner : public Cmd
{
public:
    CmdRunner(int argc, const char** argv, const char* version);

    bool run()
    {
        poptString logType;
        poptString logLevel;
        poptString logFile;
        poptString sipAddress;

        struct poptOption tableFileLog[] = {
            { "log-level",        'l', POPT_ARG_STRING, &logLevel,           0, "specify the log level, default is `info`",                 "debug|info|warning|alert" },
            { "log-file",         'f', POPT_ARG_STRING, &logFile,            0, "specify the log file name, default is `sbc.log`",          "sbc.log" },
            { "log-max-size",     's', POPT_ARG_LONG,   &mLogFileSize,       0, "specify the log file max size, default is 5242880(5M)",    "5242880" },
            { "keep-all-log",     'k', POPT_ARG_NONE,   &mKeepAllLogFiles,   0, "keep all the log file, default is no",                     0 },
            POPT_TABLEEND
        };

        struct poptOption tableSipAddr[] = {
            { "addr",        'a', POPT_ARG_STRING,  &sipAddress,    0, "Local IP Address to bind SIP transports to, sbc will bind to all adapters if not specified",    0 },
            { "udp-port",    'u', POPT_ARG_INT,     &mSipUdpPort,   0, "Local port to listen on for SIP messages over UDP - 0 to disable, default is `55555`",          "55555" },
            { "tcp-port",    't', POPT_ARG_INT,     &mSipTcpPort,   0, "Local port to listen on for SIP messages over TCP - 0 to disable, default is `55555`",          "55555" },
            POPT_TABLEEND
        };

        const struct poptOption table[] = {
            { "log-type",         'o', POPT_ARG_STRING,         &logType,           0,  "where to send logging messages, default is `file`",    "cout|file" },
            { NULL,              '\0', POPT_ARG_INCLUDE_TABLE,  tableFileLog,       0,  "options for '--log-type=file'",                        0 },
            { NULL,              '\0', POPT_ARG_INCLUDE_TABLE,  tableSipAddr,       0,  "options for sipstack configuration",                   0 },
            {"version",           'v', POPT_ARG_NONE,           0,                'v',  "show version",                                         0 },
            { "help",             'h', POPT_ARG_NONE,           NULL,             'h',  "Show this help message",                               NULL },
            { "usage",           '\0', POPT_ARG_NONE,           NULL,             'u',  "Display brief usage message",                          NULL },
            POPT_TABLEEND
        };

        if (!parseAndExec(table))
        {
            return false;
        }

        if (logType) { mLogType = logType; }
        if (logLevel) { mLogLevel = logLevel; }
        if (logFile) { mLogFile = logFile; }
        if (sipAddress) { mSipAddress = sipAddress; }

        return true;
    }
    resip::Data mLogType;
    resip::Data mLogLevel;
    resip::Data mLogFile;
    long mLogFileSize;
    int mKeepAllLogFiles;
    resip::Data mSipAddress;
    int mSipUdpPort;
    int mSipTcpPort;
protected:
    bool processOneOption(poptContext ctx, int ret);
    resip::Data mVersion;
};


class CmdReg : public Cmd
{
public:
    CmdReg(bool showUsage = false) : Cmd(Reg, showUsage) {}
    CmdReg(int argc, const char** argv, SimpleSBC* sbc);
    bool run()
    {
        poptString aor;
        poptString passwd;
        poptString displayName;
        poptString obProxy;
        poptString contact;
        const struct poptOption table[] = {
            { "aor",            'a', POPT_ARG_STRING,   &aor,           0,  "address of record, like as `sip:alice@example.com`",   0 },
            { "password",       'p', POPT_ARG_STRING,   &passwd,        0,  "password for address of record",                       0},
            { "display-name",   'n', POPT_ARG_STRING,   &displayName,   0,  "display name of address of record",                    0 },
            { "outbound-proxy", 'o', POPT_ARG_STRING,   &obProxy,       0,  "specify uri for outbound proxy, like as `sip:127.0.0.1:55555` or `sip:alice@example.com;transport=tcp`",   0 },
            { "contact",        'c', POPT_ARG_STRING,   &contact,       0,  "override default contact uri",                         0 },
            //getHelpTable(),
            { "help",             'h', POPT_ARG_NONE,           NULL,             'h',  "Show this help message",                               NULL },
            { "usage",            'u', POPT_ARG_NONE,           NULL,             'u',  "Display brief usage message",                          NULL },
            POPT_TABLEEND
        };

        if (!parseAndExec(table))
        {
            return false;
        }

        if (aor) { mAor = aor; }
        if (passwd) { mPasswd = passwd; }
        if (displayName) { mDisplayName = displayName; }
        if (obProxy) { mOutboundProxy = obProxy; }
        if (contact) { mContact = contact; }

        return true;
    }

    resip::Data mAor;
    resip::Data mPasswd;
    resip::Data mDisplayName;
    resip::Data mOutboundProxy;
    resip::Data mContact;
protected:
    bool processOneOption(poptContext ctx, int ret);
};

class CmdCall : public Cmd
{
public:
    CmdCall(bool showUsage = false) : Cmd(Call, showUsage), mStart(false), mEnd(false), mReinvite(false) {}
    CmdCall(int argc, const char** argv, SimpleSBC* sbc);
    bool run()
    {
        poptString target;
        poptString file;
        int id= 0;
        int finish = 0;
        const struct poptOption table[] = {
            { "id",     'i', POPT_ARG_NONE,     0,             'i', "Start a new call, the reg id after behind args which list in `show reg`", 0 },
            { "file",   'f', POPT_ARG_STRING,   (void*)&file,   0,  "specify an sdp text file path, use auto-generated sdp content if not specified", "./sdp.txt" },
            { "end",    'e', POPT_ARG_NONE,     0,             'e', "End specifed call, the numbers after behind args which list in `show call`", 0} ,
            { "re-invite", 'r', POPT_ARG_NONE,  0,             'r', "Re-invite an existed call, the reg id after behind args which list in `show call`", 0 },
            //getHelpTable(),
            { "help",             'h', POPT_ARG_NONE,           NULL,             'h',  "Show this help message",                               NULL },
            { "usage",            'u', POPT_ARG_NONE,           NULL,             'u',  "Display brief usage message",                          NULL },
            POPT_TABLEEND
        };

        if (!parseAndExec(table))
        {
            return false;
        }

        if (file) mFile = file;

        return exec();
    }
protected:
    const char* getReplaceHelpText() { return "[-e] [<num1> <num2>...] | [[-f ./sdp.txt] [-i|-r <num>]|[<SIP URI>]]"; }
    bool processOneOption(poptContext ctx, int ret);
    bool processNonOptionArgs(poptContext ctx);
    bool exec();
private:
    resip::Data mTarget;
    resip::Data mFile;
    bool mStart;
    bool mEnd;
    bool mReinvite;
    std::list<UInt64> mIds;
};

class CmdShow : public Cmd
{
public:
    CmdShow(bool showUsage = false) : Cmd(Show, showUsage) {}
    CmdShow(int argc, const char** argv, SimpleSBC* sbc = 0);
    bool run()
    {
        const struct poptOption table[] = {
            getHelpTable(), //no need
            POPT_TABLEEND
        };

        return parseAndExec(table);
    }
protected:
    const char* getReplaceHelpText() { return "[OPTIONS]... [reg|call]"; }
    bool processNonOptionArgs(poptContext ctx);
};

class CmdExit : public Cmd
{
public:
    CmdExit(int argc, const char** argv, SimpleSBC* sbc = 0);
    bool run()
    {
        const struct poptOption table[] = {
            POPT_TABLEEND
        };

        return parseAndExec(table);
    }
};

class CmdHelp : public Cmd
{
public:
    CmdHelp(int argc, const char** argv, SimpleSBC* sbc);
    bool run()
    {
        const struct poptOption table[] = {
            { "usage", 'u', POPT_ARG_NONE, (void*)&mUsage, 0, "Show usage or help content of specified command", 0 },
            //getHelpTable(), //no need
            POPT_TABLEEND
        };

        return parseAndExec(table);
    }
protected:
    const char* getReplaceHelpText() { return "[OPTIONS]... [reg|call|show]"; }
    bool processNonOptionArgs(poptContext ctx);
private:
    int mUsage;
};

class CmdFactory
{
public:
    static std::unique_ptr<Cmd> instanceCmd(const std::string& cmd, SimpleSBC* sbc);
    static std::unique_ptr<CmdRunner> instanceRunnerCmd(int passinArgc, char* passinArgv[], const char* version);
};


#endif // #if !defined(CMD_OPTION__H)
