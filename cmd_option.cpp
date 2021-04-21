#include "cmd_option.h"
#include "simple_sbc.h"

using namespace resip;
using namespace std;

bool Cmd::toUri(const resip::Data& input, resip::Uri& uri, resip::Data& errmsg, const resip::Data& description/* = resip::Data::Empty*/)
{
    try
    {
        uri = Uri(input);
    }
    catch (ParseException& e)
    {
        {
            oDataStream dataStream(errmsg);
            dataStream << "Can't parse " << description << " : " << input << ", Caught: " << e;
        }
        return false;
    }
    return true;
}

static const resip::Data sCmdMap[] = {
    "",
    "reg",
    "call",
    "show",
    "exit",
    "help"
};

const resip::Data& Cmd::getCmdName(const Type& type)
{
    if (type > Unknown && type < MaxType)
    {
        return sCmdMap[type];
    }
    return sCmdMap[0];
}

Cmd::Type Cmd::getCmdType(const char* cmdName)
{
    if (getCmdName(Reg) == cmdName)
    {
        return Reg;
    }
    else if (getCmdName(Call) == cmdName)
    {
        return Call;
    }
    else if (getCmdName(Show) == cmdName)
    {
        return Show;
    }
    else if (getCmdName(Exit) == cmdName)
    {
        return Exit;
    }
    else if (getCmdName(Help) == cmdName)
    {
        return Help;
    }
    else
    {
        return Unknown;
    }
}

static void displayArgs(poptContext con,
    /*@unused@*/ enum poptCallbackReason foo,
    struct poptOption * key,
    /*@unused@*/ const char * arg, /*@unused@*/ void * data)
    /*@globals fileSystem@*/
    /*@modifies fileSystem@*/
{
    if (key->shortName == 'h')
        poptPrintHelp(con, stdout, 0);
    else
        poptPrintUsage(con, stdout, 0);
}

const struct poptOption Cmd::getHelpTable()
{
    static const struct poptOption
        sHelpOptions[] = {
            { NULL,     '\0',   POPT_ARG_CALLBACK, (void *)&displayArgs,    '\0',   NULL,                           NULL },
            { "help",   'h',    0,                  NULL,                   '?',    "Show this help message",       NULL },
            { "usage",  'u',    0,                  NULL,                   'u',    "Display brief usage message",  NULL },
            POPT_TABLEEND
    };

    static const struct poptOption
        sHelpTable = { NULL, '\0', POPT_ARG_INCLUDE_TABLE, (void*)sHelpOptions, 0, "Help options:", NULL };

    return sHelpTable;
}
Cmd::Cmd(Type type, bool showUsage/* = false*/)
{
    const char* argv[] = { 
        getCmdName(type).c_str(),
        showUsage ? "--usage" : "--help"
    };
    poptDupArgv(2, argv, &mArgc, &mArgv);
    mType = type;
    mSbc = 0;
}

Cmd::Cmd(int argc, const char** argv, Type type, SimpleSBC* sbc) : mArgc(argc), mArgv(argv), mType(type), mSbc(sbc)
{
}

Cmd::~Cmd()
{
    free(mArgv);
}

Cmd::Type Cmd::getCommandType() const
{
    return mType;
}

const char* Cmd::getCommandName() const
{
    return mArgv[0];
}

bool Cmd::parseAndExec(const poptOption * table)
{
    poptContext ctx = poptGetContext(mArgv[0], mArgc, mArgv, table, 0);

    const char *helpText = this->getReplaceHelpText();
    if (helpText)
    {
        poptSetOtherOptionHelp(ctx, helpText);
    }

    int ret;
    while ((ret = poptGetNextOpt(ctx)) >= 0)
    {
        if (!this->processOneOption(ctx, ret))
            return false;
    }

    if (ret < -1) {
        /* an error occurred during option processing */
        setLastErr(poptStrerror(ret), poptBadOption(ctx, POPT_BADOPTION_NOALIAS));
        return false;
    }

    return processNonOptionArgs(ctx);
}

void Cmd::setLastErr(const char* desc, const char* arg /*= NULL*/)
{
    if (arg)
    {
        mErrors = arg;
        mErrors += ": ";
    }
    mErrors += desc;
}

//////////////////////////////////////////////////////////////////////////
CmdRunner::CmdRunner(int argc, const char** argv, const char* version)
    : Cmd(argc, argv, Unknown, 0)
    , mLogType("file")
    , mLogLevel("info")
    , mLogFile("sbc.log")
    , mLogFileSize(5242880)
    , mKeepAllLogFiles(0)
    , mSipUdpPort(55555)
    , mSipTcpPort(55555)
    , mVersion(version ? version : "")
{
}

bool CmdRunner::processOneOption(poptContext ctx, int ret)
{
    switch (ret)
    {
    case 'v':
        cerr << mVersion << endl;
        exit(0);
    case 'h':
        poptPrintHelp(ctx, stderr, 0);
        exit(0);
    case 'u':
        poptPrintUsage(ctx, stderr, 0);
        exit(0);
    default:
        break;
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
CmdReg::CmdReg(int argc, const char** argv, SimpleSBC* sbc) : Cmd(argc, argv, Reg, 0)
{

}

bool CmdReg::processOneOption(poptContext ctx, int ret)
{
    switch (ret)
    {
    case 'h':
        poptPrintHelp(ctx, stderr, 0);
        return false;
    case 'u':
        poptPrintUsage(ctx, stderr, 0);
        return false;
    default:
        break;
    }
    return true;
}

//////////////////////////////////////////////////////////////////////////
CmdCall::CmdCall(int argc, const char** argv, SimpleSBC* sbc) : Cmd(argc, argv, Call, sbc), mStart(false), mEnd(false), mReinvite(false)
{
}

bool CmdCall::processOneOption(poptContext ctx, int ret)
{
    switch (ret)
    {
    case 'i':
    {
        if (mEnd)
        {
            setLastErr("Cannot exist at the same time with flag -e|--end", "-i|--id");
            return false;
        }
        mStart = true;
    }
        break;
    case 'e':
        if (mStart)
        {
            setLastErr("Cannot exist at the same time with flag -i|--id", "-e|--end");
            return false;
        }
        mEnd = true;
        break;
    case 'r':
        if (mStart || mEnd)
        {
            setLastErr("Cannot exist at the same time with flag -i|--id or -e|--end", "-r|--re-invite");
            return false;
        }
        mReinvite = true;
        break;
    case 'h':
        poptPrintHelp(ctx, stderr, 0);
        return false;
    case 'u':
        poptPrintUsage(ctx, stderr, 0);
        return false;
    default:
        break;
    }
    return true;
}

bool CmdCall::processNonOptionArgs(poptContext ctx)
{
    const char* arg = poptGetArg(ctx);
    if (mEnd)
    {
        while (arg)
        {
            UInt64 id = Data(arg).convertUInt64();
            if (id)
            {
                mIds.push_back(id);
            }
            arg = poptGetArg(ctx);
        }
    }
    else
    {
        if (arg)
        {
            if (poptPeekArg(ctx))
            {
                setLastErr("Must specify a single target");
                return false;
            }
            mTarget = arg;
        }
    }
    return true;
}

bool CmdCall::exec()
{
    if (mEnd)
    {
        if (mIds.empty())
        {
            setLastErr("Must specify valid id");
            return false;
        }
        mSbc->finishCall(mIds);
        return true;
    }
    else
    {
        if (mStart)
        {
            return mSbc->makeNewCall(mTarget.convertUInt64(), mFile);
        }
        else
        {
            if (mReinvite)
            {
                return mSbc->makeReinvite(mTarget.convertUInt64(), mFile);
            }
            resip::Uri aor;
            resip::Data err;
            if (!toUri(mTarget, aor, err, "aor"))
            {
                setLastErr(err.c_str());
                return false;
            }
            return mSbc->makeNewCall(aor, mFile);
        }
    }
}

//////////////////////////////////////////////////////////////////////////
CmdShow::CmdShow(int argc, const char** argv, SimpleSBC* sbc /*= 0*/) : Cmd(argc, argv, Show, sbc)
{
}

bool CmdShow::processNonOptionArgs(poptContext ctx)
{
    const char* arg = poptGetArg(ctx);
    if (!arg)
    {
        return false;
    }

    if (arg && poptPeekArg(ctx))
    {
        setLastErr("Specify a single command: .e.g., show call");
        return false;
    }

    unique_ptr<Cmd> childCmd;
    if (getCmdName(Reg) == arg)
    {
        mSbc->showAllReg();
    }
    else if(getCmdName(Call) == arg)
    {
        mSbc->showAllCall();
    }
    else
    {
        setLastErr("Unknown command", arg);
        return false;
    }

    return true;
}

//////////////////////////////////////////////////////////////////////////
CmdExit::CmdExit(int argc, const char** argv, SimpleSBC* sbc) :Cmd(argc, argv, Exit, sbc)
{
}

//////////////////////////////////////////////////////////////////////////
CmdHelp::CmdHelp(int argc, const char** argv, SimpleSBC* sbc) : Cmd(argc, argv, Help, sbc), mUsage(0)
{
}

bool CmdHelp::processNonOptionArgs(poptContext ctx)
{
    const char* arg = poptGetArg(ctx);
    if (!arg)
    {
        if (mUsage)
            poptPrintUsage(ctx, stderr, 0);
        else
            poptPrintHelp(ctx, stderr, 0);
        return false;
    }

    if (arg && poptPeekArg(ctx))
    {
        setLastErr("Specify a single command: .e.g., help call");
        return false;
    }

    unique_ptr<Cmd> childCmd;
    if (getCmdName(Reg) == arg)
    {
        childCmd = unique_ptr<Cmd>(new CmdReg(mUsage));
    }
    else if (getCmdName(Call) == arg)
    {
        childCmd = unique_ptr<Cmd>(new CmdCall(mUsage));
    }
    else if (getCmdName(Show) == arg)
    {
        childCmd = unique_ptr<Cmd>(new CmdShow(mUsage));
    }
    else
    {
        setLastErr("Unknown command", arg);
        return false;
    }

    if (childCmd && !childCmd->run())
    {
        setLastErr(childCmd->getLastErr());
        return false;
    }

    return true;
}


//////////////////////////////////////////////////////////////////////////
unique_ptr<Cmd> CmdFactory::instanceCmd(const string& cmd, SimpleSBC* sbc)
{
    int argc = 0;
    const char** argv = 0;
    int ret = poptParseArgvString(cmd.c_str(), &argc, &argv);
    if (ret)
    {
        cerr << poptStrerror(ret) << endl;
        free(argv);
        return NULL;
    }

    unique_ptr<Cmd> inst;
    Cmd::Type t = Cmd::getCmdType(argv[0]);
    switch (t)
    {
    case Cmd::Reg:
        inst = unique_ptr<Cmd>(new CmdReg(argc, argv, sbc));
        break;
    case Cmd::Call:
        inst = unique_ptr<Cmd>(new CmdCall(argc, argv, sbc));
        break;
    case Cmd::Show:
        inst = unique_ptr<Cmd>(new CmdShow(argc, argv, sbc));
        break;
    case Cmd::Exit:
        inst = unique_ptr<Cmd>(new CmdExit(argc, argv, sbc));
        break;
    case Cmd::Help:
        inst = unique_ptr<Cmd>(new CmdHelp(argc, argv, sbc));
        break;
    default:
        cerr << "Unknown command: " << argv[0] << ", Type 'help' for detail command" << endl;
        break;
    }

    return inst;
}

std::unique_ptr<CmdRunner> CmdFactory::instanceRunnerCmd(int passinArgc, char* passinArgv[], const char* version)
{
    int argc = 0;
    const char** argv = 0;
    poptDupArgv(passinArgc, (const char**)passinArgv, &argc, &argv);
    return std::unique_ptr<CmdRunner>(new CmdRunner(argc, argv, version));
}


