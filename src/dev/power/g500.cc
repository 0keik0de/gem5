#include "dev/power/g500.hh"

#include "cpu/intr_control.hh"
#include "sim/system.hh"

using namespace std;

G500::G500(const Params *p)
    : Platform(p), system(p->system)
{}

void
G500::postConsoleInt()
{
    //warn_once("Don't know what interrupt to post for console.\n");
    cout<<"Post console intr\n";
    this->intrctrl->post(2,0);
    //panic("Need implementation\n");
}

void
G500::clearConsoleInt()
{
    //warn_once("Don't know what interrupt to clear for console.\n");
    //cout<<"Clear console intr\n";
    this->intrctrl->clear(2,0);
    //panic("Need implementation\n");
}

void
G500::postPciInt(int line)
{
    panic("Need implementation\n");
}

void
G500::clearPciInt(int line)
{
    panic("Need implementation\n");
}

Addr
G500::pciToDma(Addr pciAddr) const
{
    panic("Need implementation\n");
    M5_DUMMY_RETURN
}


Addr
G500::calcPciConfigAddr(int bus, int dev, int func)
{
    panic("Need implementation\n");
    M5_DUMMY_RETURN
}

Addr
G500::calcPciIOAddr(Addr addr)
{
    panic("Need implementation\n");
    M5_DUMMY_RETURN
}

Addr
G500::calcPciMemAddr(Addr addr)
{
    panic("Need implementation\n");
    M5_DUMMY_RETURN
}

G500 *
G500Params::create()
{
    return new G500(this);
}
