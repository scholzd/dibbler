/*                                                                           
 * Dibbler - a portable DHCPv6                                               
 *                                                                           
 * authors: Tomasz Mrugalski <thomson@klub.com.pl>                           
 *          Marek Senderski <msend@o2.pl>                                    
 * changes: Krzysztof Wnuk <keczi@poczta.onet.pl>                                                                         
 * released under GNU GPL v2 or later licence                                
 *                                                                           
 * $Id: ClntCfgMgr.cpp,v 1.43 2006-11-03 23:14:39 thomson Exp $
 *
 */

#include <iostream>
#include <fstream>
#include <string>
#include "SmartPtr.h"
#include "Portable.h"
#include "ClntCfgMgr.h"
#include "ClntCfgIface.h"
#include "Logger.h"
#include "FlexLexer.h"

using namespace std;

#include "IfaceMgr.h"
#include "ClntIfaceMgr.h"

#include "ClntParsGlobalOpt.h"
#include "TimeZone.h"

#include "FlexLexer.h"
#include "ClntParser.h"

#ifdef MOD_CLNT_EMBEDDED_CFG
static bool HardcodedCfgExample(TClntCfgMgr *cfgMgr, string params);
#endif

TClntCfgMgr::TClntCfgMgr(SmartPtr<TClntIfaceMgr> ClntIfaceMgr, 
                         const string cfgFile, const string oldCfgFile)
    :TCfgMgr((Ptr*)ClntIfaceMgr)
{
    this->IfaceMgr = ClntIfaceMgr;
    this->IsDone=false;

    /* support for config changes between runs - currently disabled */
    // bool newConf=false; //newConf=true if files differs
    // newConf=compareConfigs(cfgFile,oldCfgFile);
    // if(newConf) 
    //   this->copyFile(cfgFile,oldCfgFile);
    /* support for config changes between runs - currently disabled */

#ifndef MOD_CLNT_EMBEDDED_CFG
    // --- normal operation: read config. file ---

    // parse config file
    ifstream f;
    f.open(cfgFile.c_str());
    if ( ! f.is_open()  ) {
	Log(Crit) << "Unable to open " << cfgFile << " file." << LogEnd; 
	this->IsDone = true;
	return;
    } else {
	Log(Notice) << "Parsing " << cfgFile << " config file..." << LogEnd;
    }
    yyFlexLexer lexer(&f,&clog);
    ClntParser parser(&lexer);
    int result = parser.yyparse();
    Log(Debug) << "Parsing " << cfgFile << " done, result=" << result << LogEnd;
    f.close();

    if (result) {
        //Result!=0 means config errors. Finish whole DHCPClient 
        Log(Crit) << "Fatal error during config parsing." << LogEnd;
        this->IsDone = true; 
        this->DUID=new TDUID();
        return;
    }

    // match parsed interfaces with interfaces detected in system
    matchParsedSystemInterfaces(&parser);
#else
    // --- use hardcoded config ---
    // use your favourite configuration generator function here
    HardcodedCfgFunc *cfgMaker = &HardcodedCfgExample;

    // call your function here
    cfgMaker(this, cfgFile);

#endif
    this->LogLevel = logger::getLogLevel();
    this->LogName  = logger::getLogName();
  
    // check config consistency
    if(!validateConfig()) {
        this->IsDone=true;
        return;
    }

    // load or create DUID
    string duidFile = (string)CLNTDUID_FILE;
    if (!setDUID(duidFile)) {
	this->IsDone=true;
	return;
    }
    this->dump();
    
    IsDone = false;
}

void TClntCfgMgr::dump() {
    // store ClntCfgMgr in file
    std::ofstream xmlDump;
    xmlDump.open(CLNTCFGMGR_FILE);
    xmlDump << *this;
    xmlDump.close();
}

/*
  match parsed interfaces with interfaces detected in system. 
  CfgIface objects are created placed in CfgMgr. 
*/
bool TClntCfgMgr::matchParsedSystemInterfaces(ClntParser *parser) {
    int cfgIfaceCnt;

    // copy global options here
    
    // user has specified DUID type, just in case if new DUID will be generated
    if (parser->DUIDType != DUID_TYPE_NOT_DEFINED) {
	this->DUIDType = parser->DUIDType;
	//Log(Debug) << "DUID type set to " << parser->DUIDType << "." << LogEnd;
    }
    cfgIfaceCnt = parser->ClntCfgIfaceLst.count();
    Log(Debug) << cfgIfaceCnt << " interface(s) specified in " << CLNTCONF_FILE << LogEnd;

    SmartPtr<TClntCfgIface> cfgIface;
    SmartPtr<TIfaceIface> ifaceIface;

    if (cfgIfaceCnt) {
	// user specified some interfaces in config file
	parser->ClntCfgIfaceLst.first();
	while(cfgIface = parser->ClntCfgIfaceLst.get()) {
	    // for each interface (from config file)
	    if (cfgIface->getID()==-1) {
		ifaceIface = IfaceMgr->getIfaceByName(cfgIface->getName());
	    } else {
		ifaceIface = IfaceMgr->getIfaceByID(cfgIface->getID());
	    }

	    if (!ifaceIface) {
		Log(Error) << "Interface " << cfgIface->getName() << "/" << cfgIface->getID() 
			   << " specified in " << CLNTCONF_FILE << " is not present or does not support IPv6."
			   << LogEnd;
		this->IsDone = true;
		continue;
	    }
	    if (cfgIface->noConfig()) {
		Log(Info) << "Interface " << cfgIface->getName() << "/" << cfgIface->getID() 
			       << " has flag no-config set, so it is ignored." << LogEnd;
		continue;
	    }

	    cfgIface->setIfaceName(ifaceIface->getName());
	    cfgIface->setIfaceID(ifaceIface->getID());

	    ifaceIface->setPrefixLength(cfgIface->getPrefixLength());

	    if (!ifaceIface->countLLAddress()) {
		Log(Crit) << "Interface " << ifaceIface->getName() << "/" << ifaceIface->getID() 
			  << " is down or doesn't have any link-layer address." << LogEnd;
		this->IsDone = true;
		continue;
	    }

	    this->addIface(cfgIface);
	    Log(Info) << "Interface " << cfgIface->getName() << "/" << cfgIface->getID() 
			 << " configuation has been loaded." << LogEnd;
	}
    } else {
	// user didn't specified any interfaces in config file, so
	// we'll try to configure each interface we could find
	Log(Warning) << "Config file does not contain any interface definitions."
		     << LogEnd;
	
	IfaceMgr->firstIface();
	while ( ifaceIface = IfaceMgr->getIface() ) {
	    // for each interface present in the system...
	    if (!ifaceIface->flagUp()) {
		Log(Notice) << "Interface " << ifaceIface->getName() << "/" << ifaceIface->getID() 
			    << " is down, ignoring." << LogEnd;
		continue;
	    }
	    if (!ifaceIface->flagRunning()) {
		Log(Notice) << "Interface " << ifaceIface->getName() << "/" << ifaceIface->getID() 
			    << " has flag RUNNING not set, ignoring." << LogEnd;
		continue;
	    }
	    if (!ifaceIface->flagMulticast()) {
		Log(Notice) << "Interface " << ifaceIface->getName() << "/" << ifaceIface->getID() 
                            << " is not multicast capable, ignoring." << LogEnd;
		continue;
	    }
	    if ( !(ifaceIface->getMacLen() > 5) ) {
		Log(Notice) << "Interface " << ifaceIface->getName() << "/" << ifaceIface->getID() 
                            << " has MAC address length " << ifaceIface->getMacLen() 
                            << " (6 or more required), ignoring." << LogEnd;
		continue;
	    }

	    // One address...
	    SmartPtr<TClntCfgAddr> addr(new TClntCfgAddr());
	    addr->setOptions(parser->ParserOptStack.getLast());

	    // ... is stored in one IA...
	    SmartPtr<TClntCfgIA> ia = new TClntCfgIA();
	    ia->setOptions(parser->ParserOptStack.getLast());
	    ia->addAddr(addr);
	    
	    // ... on this newly created interface...
	    cfgIface = SmartPtr<TClntCfgIface>(new TClntCfgIface(ifaceIface->getID()));
	    cfgIface->setIfaceName(ifaceIface->getName());
	    cfgIface->setIfaceID(ifaceIface->getID());
	    cfgIface->addIA(ia);
	    cfgIface->setOptions(parser->ParserOptStack.getLast());

	    // ... which is added to ClntCfgMgr
	    this->addIface(cfgIface);

	    Log(Info) << "Interface " << cfgIface->getName() << "/" << cfgIface->getID() 
                      << " has been added." << LogEnd;
	}
    }
    return true;
}

SmartPtr<TClntCfgIface> TClntCfgMgr::getIface()
{
    return ClntCfgIfaceLst.get();
}

void TClntCfgMgr::addIface(SmartPtr<TClntCfgIface> ptr)
{
    ClntCfgIfaceLst.append(ptr);
}

void TClntCfgMgr::firstIface()
{
    ClntCfgIfaceLst.first();
}

int TClntCfgMgr::countIfaces()
{
    return ClntCfgIfaceLst.count();
}

bool TClntCfgMgr::getReconfigure()
{
    //FIXME: Implement this in some distant future
    return false;
}

int TClntCfgMgr::countAddrForIA(long IAID)
{
    SmartPtr<TClntCfgIface> iface;
    firstIface();
    while (iface = getIface() ) 
    {
	SmartPtr<TClntCfgIA> ia;
	iface->firstIA();
	while (ia = iface->getIA())
	    if (ia->getIAID()==IAID)
		return ia->countAddr();
    }    
    return 0;
}

SmartPtr<TClntCfgIA> TClntCfgMgr::getIA(long IAID)
{
    SmartPtr<TClntCfgIface> iface;
    firstIface();
    while (iface = getIface() ) 
    {
	SmartPtr<TClntCfgIA> ia;
	iface->firstIA();
	while (ia = iface->getIA())
	    if (ia->getIAID()==IAID)
		return ia;
    }        
    return 0;
}

SmartPtr<TClntCfgPD> TClntCfgMgr::getPD(long IAID)
{
    SmartPtr<TClntCfgIface> iface;
    firstIface();
    while (iface = getIface() ) 
    {
	SmartPtr<TClntCfgPD> pd;
	iface->firstPD();
	while (pd = iface->getPD())
	    if (pd->getIAID()==IAID)
		return pd;
    }        
    return 0;
}

bool TClntCfgMgr::setIAState(int iface, int iaid, enum EState state)
{
    firstIface();
    SmartPtr<TClntCfgIface> ptrIface;
    while (ptrIface = getIface() ) {
        if ( ptrIface->getID() == iface ) break;
    }
    if (! ptrIface) {
	Log(Error) <<"ClntCfgMgr: Unable to set IA state (id=" << iaid 
            << "):Interface " << iface << " not found." << LogEnd;
        return false;
    }

    SmartPtr<TClntCfgIA> ia;
    ptrIface->firstIA();

    while (ia = ptrIface->getIA()) 
    {
	if ( ia->getIAID() == iaid ) {
	    ia->setState(state);
	    return true;
	}
    }

    Log(Error) << "ClntCfgMgr: Unable to set IA state (id=" << iaid << ")" << LogEnd;
    return false;
}	    

//check whether T1<T2 and Pref<Valid and at least T1<=Valid
bool TClntCfgMgr::validateConfig()
{
    //Is everything so far is ok
    if (IsDone) return false;
    SmartPtr<TClntCfgIface> ptrIface;
    this->ClntCfgIfaceLst.first();
    while(ptrIface=ClntCfgIfaceLst.get())
    {
	if (!this->validateIface(ptrIface)) {
	    return false;
	}
    }
    return true;
}

bool TClntCfgMgr::validateIface(SmartPtr<TClntCfgIface> ptrIface) {

    if(ptrIface->isReqTimezone()&&(ptrIface->getProposedTimezone()!=""))
    {   
	TTimeZone tmp(ptrIface->getProposedTimezone());
	if(!tmp.isValid())
	{
	    Log(Crit) << "Wrong time zone option for the " << ptrIface->getName() 
		      << "/" <<ptrIface->getID() << " interface." << LogEnd;
	    return false;
	}
    }
    
    SmartPtr<TClntCfgIA> ptrIA;
    ptrIface->firstIA();
    while(ptrIA=ptrIface->getIA())
    {
	if (!this->validateIA(ptrIface, ptrIA)) 
	    return false;
    }
    return true;
}

bool TClntCfgMgr::validateIA(SmartPtr<TClntCfgIface> ptrIface, SmartPtr<TClntCfgIA> ptrIA) {

    if ( ptrIA->getT2()<ptrIA->getT1() ) 
    {
	Log(Crit) << "T1 can't be lower than T2 for IA " << *ptrIA << "on the " << ptrIface->getName() 
		  << "/" << ptrIface->getID() << " interface." << LogEnd;
	return false;
    }
    SmartPtr<TClntCfgAddr> ptrAddr;
    ptrIA->firstAddr();
    while(ptrAddr=ptrIA->getAddr())
    {
	if (!this->validateAddr(ptrIface, ptrIA, ptrAddr))
	    return false;
    }
    return true;
}

bool TClntCfgMgr::validateAddr(SmartPtr<TClntCfgIface> ptrIface, 
			       SmartPtr<TClntCfgIA> ptrIA,
			       SmartPtr<TClntCfgAddr> ptrAddr) {
    SmartPtr<TIPv6Addr> addr = ptrAddr->get();
    if ( addr && addr->linkLocal()) {
	Log(Crit) << "Address " << ptrAddr->get()->getPlain() << " specified in IA "
		  << ptrIA->getIAID() << " on the " << ptrIface->getName() << "/" << ptrIface->getID()
		  << " interface is link local." << LogEnd;
	return false;
    }
    if( ptrAddr->getPref()>ptrAddr->getValid() ) {
	Log(Crit) << "Prefered time " << ptrAddr->getPref() << " can't be lower than valid lifetime " 
		  << ptrAddr->getValid() << " for IA " << ptrIA->getIAID() << " on the " 
		  << ptrIface->getName() << "/" << ptrIface->getID() << " interface." << LogEnd;
	return false;
    }
    if ((unsigned long)ptrIA->getT1()>(unsigned long)ptrAddr->getValid()) {
	Log(Crit) << "Valid lifetime " << ptrAddr->getValid() << " can't be lower than T1 " <<ptrIA->getT1()
		  << "(address can't be renewed) in IA " << ptrIA->getIAID() << " on the " 
		  << ptrIface->getName() << "/" << ptrIface->getName() << " interface." << LogEnd;
	return false;
    }
    
    return true;
}

SmartPtr<TClntCfgIface> TClntCfgMgr::getIface(int id)
{
    firstIface();
    SmartPtr<TClntCfgIface> iface;
    while(iface=getIface())
        if (iface->getID()==id) return iface;
    return SmartPtr<TClntCfgIface> ();
}

SmartPtr<TClntCfgIface> TClntCfgMgr::getIfaceByIAID(int iaid)
{
    SmartPtr<TClntCfgIface> iface;
    firstIface();
    while(iface=getIface())
    {
	SmartPtr<TClntCfgIA> ia;
	iface->firstIA();
	while(ia=iface->getIA())
	    if (ia->getIAID()==iaid)
		return iface;
    }
    return SmartPtr<TClntCfgIface>();
}

bool TClntCfgMgr::isDone() {
    return this->IsDone;
}

TClntCfgMgr::~TClntCfgMgr() {
    Log(Debug) << "ClntCfgMgr cleanup." << LogEnd;
}


ostream & operator<<(ostream &strum, TClntCfgMgr &x)
{
    strum << "<ClntCfgMgr>" << endl;
    strum << "  <workdir>" << x.getWorkDir()  << "</workdir>" << endl;
    strum << "  <LogName>" << x.getLogName()  << "</LogName>" << endl;
    strum << "  <LogLevel>" << x.getLogLevel() << "</LogLevel>" << endl;
    if (x.DUID)
        strum << "  " << *x.DUID;
    else
        strum << "  <!-- duid not set -->";

    SmartPtr<TClntCfgIface> ptr;
    x.firstIface();

    while ( ptr = x.getIface() ) {
        strum << *ptr;
    }

    strum << "</ClntCfgMgr>" << endl;
    return strum;
}


#ifdef MOD_CLNT_EMBEDDED_CFG
/** 
 * this is example hardcoded configuration file
 * 
 * @param cfgMgr 
 * @param params 
 * 
 * @return 
 */
bool HardcodedCfgExample(TClntCfgMgr *cfgMgr, string params)
{
    Log(Info) << "Using hardcoded config. file." << LogEnd;

    // there's no way to set some parameters directly, to fake ClntParsGlobalOpt
    // must be created. 
    SPtr<TClntParsGlobalOpt> opt = new TClntParsGlobalOpt();

    // Pretend to have parsed empty DNS list (i.e. request DNS server configuration, but don't
    // provide any hints)
    List(TIPv6Addr) dnsList;
    dnsList.clear();
    opt->setDNSServerLst(&dnsList);
    // Pretend to have parsed rapid-commit request
    opt->setRapidCommit(true);

    // Create interface, which will be configured
    SPtr<TClntCfgIface> iface = new TClntCfgIface("eth0");
    iface->setIfaceID(3);

    // set all "parsed" options on this interface
    iface->setOptions(opt);

    // Create one IA 
    SPtr<TClntCfgIA> ia = new TClntCfgIA();
    ia->setIAID(123);

    // set parameters in the "parsed" objects
    opt->setT1(900);
    opt->setT2(1200);
    ia->setOptions(opt);

    // optional: add a requested address to IA
    // request for a 2000::123:456 address with preferred lifetime set to 1800
    // and valid lifetime set to 3600
    SPtr<TIPv6Addr> addr = new TIPv6Addr("2000::123:456", true);
    SPtr<TClntCfgAddr> cfgAddr = new TClntCfgAddr(addr, 3600, 1800);
    ia->addAddr(cfgAddr);

    // add IA to a interface
    iface->addIA(ia);

    // add interface to a CfgMgr
    cfgMgr->addIface(iface);

    return true;
}
#endif
