/*
 * Dibbler - a portable DHCPv6
 *
 * authors: Tomasz Mrugalski <thomson@klub.com.pl>
 *          Marek Senderski <msend@o2.pl>
 * changes: Nguyen Vinh Nghiem
 *
 * released under GNU GPL v2 only licence
 *
 */

#include "SrvCfgAddrClass.h"
#include "SmartPtr.h"
#include "SrvParsGlobalOpt.h"
#include "DHCPConst.h"
#include "Logger.h"
#include "SrvOptAddrParams.h"
#include "SrvMsg.h"
#include "DHCPDefaults.h"

using namespace std;

// static field initialization
unsigned long TSrvCfgAddrClass::StaticID_ = 0;

TSrvCfgAddrClass::TSrvCfgAddrClass() {
    T1Beg_    = 0;
    T1End_    = DHCPV6_INFINITY;
    T2Beg_    = 0;
    T2End_    = DHCPV6_INFINITY;
    PrefBeg_  = 0;
    PrefEnd_  = DHCPV6_INFINITY;
    ValidBeg_ = 0;
    ValidEnd_ = DHCPV6_INFINITY;
    ID_ = StaticID_++; // client-class ID
    AddrsAssigned_ = 0;
    AddrsCount_ = 0;
    Share_ = 100;
    ClassMaxLease_ = SERVER_DEFAULT_CLASSMAXLEASE;
}

TSrvCfgAddrClass::~TSrvCfgAddrClass() {
}

/*
 * is client allowed to use this class? (it can be rejected on DUID or address basis)
 */
bool TSrvCfgAddrClass::clntSupported(SPtr<TDUID> duid,SPtr<TIPv6Addr> clntAddr)
{
    SPtr<THostRange> range;
    RejedClnt_.first();
    // is client on black list?
    while(range = RejedClnt_.get())
        if (range->in(duid,clntAddr))
            return false;

    if (AcceptClnt_.count()) {
        AcceptClnt_.first();
            // there's white list
        while(range = AcceptClnt_.get()) {
            // is client on this white list?
            if (range->in(duid,clntAddr))
                return true;
        }
        return false;
    }

    return true;
}

bool TSrvCfgAddrClass::clntSupported(SPtr<TDUID> duid,SPtr<TIPv6Addr> clntAddr, SPtr<TSrvMsg> msg)
{

    // is client on denied client class
        SPtr<TSrvCfgClientClass> clntClass;
        DenyClientClassLst_.first();
        while(clntClass = DenyClientClassLst_.get())
        {
                if (clntClass->isStatisfy(msg))
                return false;
        }

        // is client on accepted client class
        AllowClientClassLst_.first();
        while(clntClass = AllowClientClassLst_.get())
        {
                if (clntClass->isStatisfy(msg))
                        return true;
        }

    SPtr<THostRange> range;
    RejedClnt_.first();

    // is client on black list?
    while(range = RejedClnt_.get())
        if (range->in(duid,clntAddr))
            return false;

    if (AcceptClnt_.count()) {
        AcceptClnt_.first();
            // there's white list
        while(range = AcceptClnt_.get()) {
            // is client on this white list?
            if (range->in(duid,clntAddr))
                return true;
        }
        return false;
    }

    if (AllowClientClassLst_.count())
        return false ;
   return true;

}
/*
 * is client prefered in this class? (= is it in whitelist?)
 */
bool TSrvCfgAddrClass::clntPrefered(SPtr<TDUID> duid,SPtr<TIPv6Addr> clntAddr)
{
    SPtr<THostRange> range;
    RejedClnt_.first();
    // is client on black list?
    while(range = RejedClnt_.get())
        if (range->in(duid,clntAddr))
            return false;

    if (AcceptClnt_.count()) {
        AcceptClnt_.first();
        while(range = AcceptClnt_.get()) {
            if (range->in(duid,clntAddr))
                return true;
        }
        return false;
    } else {
        return false;
    }
}


long TSrvCfgAddrClass::chooseTime(unsigned long beg, unsigned long end, unsigned long clntTime)
{
    if (clntTime < beg)
        return beg;
    if (clntTime > end)
        return end;
    return clntTime;
}

unsigned long TSrvCfgAddrClass::getT1(unsigned long clntT1) {
    return chooseTime(T1Beg_, T1End_, clntT1);
}

unsigned long TSrvCfgAddrClass::getT2(unsigned long clntT2) {
    return chooseTime(T2Beg_, T2End_, clntT2);
}

unsigned long TSrvCfgAddrClass::getPref(unsigned long clntPref) {
    return chooseTime(PrefBeg_, PrefEnd_, clntPref);
}

unsigned long TSrvCfgAddrClass::getValid(unsigned long clntValid) {
    return chooseTime(ValidBeg_, ValidEnd_, clntValid);
}

void TSrvCfgAddrClass::setOptions(SPtr<TSrvParsGlobalOpt> opt)
{
    T1Beg_    = opt->getT1Beg();
    T2Beg_    = opt->getT2Beg();
    T1End_    = opt->getT1End();
    T2End_    = opt->getT2End();
    PrefBeg_  = opt->getPrefBeg();
    PrefEnd_  = opt->getPrefEnd();
    ValidBeg_ = opt->getValidBeg();
    ValidEnd_ = opt->getValidEnd();
    Share_    = opt->getShare();

    AllowLst_ = opt->getAllowClientClassString();
    DenyLst_  = opt->getDenyClientClassString();

    ClassMaxLease_ = opt->getClassMaxLease();

    SPtr<THostRange> statRange;
    opt->firstRejedClnt();
    while(statRange = opt->getRejedClnt())
        RejedClnt_.append(statRange);

    opt->firstAcceptClnt();
    while(statRange = opt->getAcceptClnt())
        AcceptClnt_.append(statRange);

    opt->firstPool();
    Pool_ = opt->getPool();
    if (opt->getPool()) {
        Log(Warning) << "Two or more pool defined. Only one is used." << LogEnd;
    }

    // set up address counter counts
    AddrsCount_ = Pool_->rangeCount();
    AddrsAssigned_ = 0;

    if (ClassMaxLease_ > AddrsCount_)
        ClassMaxLease_ = AddrsCount_;

    AddrParams_ = opt->getAddrParams();
}

bool TSrvCfgAddrClass::addrInPool(SPtr<TIPv6Addr> addr)
{
    return Pool_->in(addr);
}

unsigned long TSrvCfgAddrClass::countAddrInPool()
{
    return AddrsCount_;
}

SPtr<TIPv6Addr> TSrvCfgAddrClass::getRandomAddr()
{
    return Pool_->getRandomAddr();
}

unsigned long TSrvCfgAddrClass::getClassMaxLease() {
    return ClassMaxLease_;
}

unsigned long TSrvCfgAddrClass::getID()
{
    return ID_;
}

unsigned long TSrvCfgAddrClass::getShare() {
    return Share_;
}

long TSrvCfgAddrClass::incrAssigned(int count) {
    AddrsAssigned_ += count;
    return AddrsAssigned_;
}

long TSrvCfgAddrClass::decrAssigned(int count) {
    AddrsAssigned_ -= count;
    return AddrsAssigned_;
}

unsigned long TSrvCfgAddrClass::getAssignedCount() {
    return AddrsAssigned_;
}

bool TSrvCfgAddrClass::isLinkLocal() {
    SPtr<TIPv6Addr> addr = new TIPv6Addr("fe80::",true);
    if (addrInPool(addr)) {
        Log(Crit) << "Link local address (fe80::) belongs to the class." << LogEnd;
        return true;
    }

    addr = new TIPv6Addr("fe80:ffff:ffff:ffff:ffff:ffff:ffff:ffff", true);
    if (addrInPool(addr)) {
        Log(Crit) << "Link local address (fe80:ffff:ffff:ffff:ffff:ffff:ffff:ffff) belongs to the class." << LogEnd;
        return true;
    }

    addr = Pool_->getAddrL();
    char linklocal[] = { 0xfe, 0x80};

    if (!memcmp(addr->getAddr(), linklocal,2)) {
        Log(Crit) << "Staring address " << addr->getPlain() << " is link-local." << LogEnd;
        return true;
    }

    addr = Pool_->getAddrR();
    if (!memcmp(addr->getAddr(), linklocal,2)) {
        Log(Crit) << "Ending address " << addr->getPlain() << " is link-local." << LogEnd;
        return true;
    }

    return false;
}

SPtr<TSrvOptAddrParams> TSrvCfgAddrClass::getAddrParams()
{
    return AddrParams_;
}


ostream& operator<<(ostream& out,TSrvCfgAddrClass& addrClass)
{
    out << "    <class id=\"" << addrClass.ID_ << "\" share=\"" << addrClass.Share_ << "\">" << std::endl;
    out << "      <!-- total addrs in class: " << addrClass.AddrsCount_
        << ", addrs assigned: " << addrClass.AddrsAssigned_ << " -->" << endl;
    out << "      <T1 min=\"" << addrClass.T1Beg_ << "\" max=\"" << addrClass.T1End_  << "\" />" << endl;
    out << "      <T2 min=\"" << addrClass.T2Beg_ << "\" max=\"" << addrClass.T2End_  << "\" />" << endl;
    out << "      <pref min=\"" << addrClass.PrefBeg_ << "\" max=\""<< addrClass.PrefEnd_  << "\" />" <<endl;
    out << "      <valid min=\"" << addrClass.ValidBeg_ << "\" max=\""<< addrClass.ValidEnd_ << "\" />" << endl;
    out << "      <ClassMaxLease>" << addrClass.ClassMaxLease_ << "</ClassMaxLease>" << endl;

    SPtr<THostRange> statRange;
    out << "      <!-- address range -->" << endl;
    out << *addrClass.Pool_;

    out << "      <!-- reject-clients ranges:" << addrClass.RejedClnt_.count() << " -->" << endl;
    addrClass.RejedClnt_.first();
    while(statRange=addrClass.RejedClnt_.get())
        out << *statRange;

    out << "      <!-- accept-only ranges:" << addrClass.AcceptClnt_.count() << " -->" << endl;
    addrClass.AcceptClnt_.first();
    while(statRange=addrClass.AcceptClnt_.get())
        out << *statRange;

    if (addrClass.AddrParams_)
        out << "      <AddrParams prefix=\"" << addrClass.AddrParams_->getPrefix() << "\" bitfield=\""
            << addrClass.AddrParams_->getBitfield() << "\"/>" << endl;
    out << "    </class>" << std::endl;
    return out;
}

/**
 * Create the AllowClientClassLst and DenyClientClassLst
 *
 * @param clientClassLst list of available client class names
 */
void TSrvCfgAddrClass::mapAllowDenyList( List(TSrvCfgClientClass) clientClassLst )
{

    Log(Info) << "Mapping allow, deny list to class "<< ID_ << ":" << clientClassLst.count()
              << " allow/deny entries in total." << LogEnd;

    SPtr<string> classname;
    SPtr<TSrvCfgClientClass> clntClass;

    AllowLst_.first();
    while (classname = AllowLst_.get())
    {
        clientClassLst.first();
        while( clntClass = clientClassLst.get() )
        {
            if (clntClass->getClassName()== *classname)
            {
                AllowClientClassLst_.append(clntClass);
                Log(Debug) << "  Insert into allow list " <<clntClass->getClassName() << LogEnd;
            }
        }
    }

    DenyLst_.first();
    while (classname = DenyLst_.get())
    {
        clientClassLst.first();
        while( clntClass = clientClassLst.get() )
        {
            if (clntClass->getClassName()== *classname)
            {
                DenyClientClassLst_.append(clntClass);
                Log(Debug) << "  Insert into deny list " <<clntClass->getClassName()<<LogEnd;
            }
        }
    }
}
