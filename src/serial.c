// 16bit code to handle serial and printer services.
//
// Copyright (C) 2008,2009  Kevin O'Connor <kevin@koconnor.net>
// Copyright (C) 2002  MandrakeSoft S.A.
//
// This file may be distributed under the terms of the GNU LGPLv3 license.

#include "biosvar.h" // SET_BDA
#include "util.h" // debug_enter
#include "bregs.h" // struct bregs


/****************************************************************
 * COM ports
 ****************************************************************/

static u16
detect_serial(u16 port, u8 timeout, u8 count)
{
    outb(0x02, port+SEROFF_IER);
    u8 ier = inb(port+SEROFF_IER);
    if (ier != 0x02)
        return 0;
    u8 iir = inb(port+SEROFF_IIR);
    if ((iir & 0x3f) != 0x02)
        return 0;

    outb(0x00, port+SEROFF_IER);
    SET_BDA(port_com[count], port);
    SET_BDA(com_timeout[count], timeout);
    return 1;
}

void
serial_setup()
{
    if (! CONFIG_SERIAL)
        return;
    dprintf(3, "init serial\n");

    u16 count = 0;
    count += detect_serial(PORT_SERIAL1, 0x0a, count);
    count += detect_serial(PORT_SERIAL2, 0x0a, count);
    count += detect_serial(PORT_SERIAL3, 0x0a, count);
    count += detect_serial(PORT_SERIAL4, 0x0a, count);
    dprintf(1, "Found %d serial ports\n", count);

    // Equipment word bits 9..11 determing # serial ports
    u16 eqb = GET_BDA(equipment_list_flags);
    SET_BDA(equipment_list_flags, (eqb & 0xf1ff) | (count << 9));
}

static u16
getComAddr(struct bregs *regs)
{
    if (regs->dx >= 4) {
        set_fail(regs);
        return 0;
    }
    u16 addr = GET_BDA(port_com[regs->dx]);
    if (! addr)
        set_fail(regs);
    return addr;
}

// SERIAL - INITIALIZE PORT
static void
handle_1400(struct bregs *regs)
{
    u16 addr = getComAddr(regs);
    if (!addr)
        return;
    outb(inb(addr+SEROFF_LCR) | 0x80, addr+SEROFF_LCR);
    if ((regs->al & 0xE0) == 0) {
        outb(0x17, addr+SEROFF_DLL);
        outb(0x04, addr+SEROFF_DLH);
    } else {
        u16 val16 = 0x600 >> ((regs->al & 0xE0) >> 5);
        outb(val16 & 0xFF, addr+SEROFF_DLL);
        outb(val16 >> 8, addr+SEROFF_DLH);
    }
    outb(regs->al & 0x1F, addr+SEROFF_LCR);
    regs->ah = inb(addr+SEROFF_LSR);
    regs->al = inb(addr+SEROFF_MSR);
    set_success(regs);
}

// SERIAL - WRITE CHARACTER TO PORT
static void
handle_1401(struct bregs *regs)
{
    u16 addr = getComAddr(regs);
    if (!addr)
        return;
    u16 timer = GET_BDA(timer_counter);
    u16 timeout = GET_BDA(com_timeout[regs->dx]);
    while (((inb(addr+SEROFF_LSR) & 0x60) != 0x60) && (timeout)) {
        u16 val16 = GET_BDA(timer_counter);
        if (val16 != timer) {
            timer = val16;
            timeout--;
        }
    }
    if (timeout)
        outb(regs->al, addr+SEROFF_DATA);
    regs->ah = inb(addr+SEROFF_LSR);
    if (!timeout)
        regs->ah |= 0x80;
    set_success(regs);
}

// SERIAL - READ CHARACTER FROM PORT
static void
handle_1402(struct bregs *regs)
{
    u16 addr = getComAddr(regs);
    if (!addr)
        return;
    u16 timer = GET_BDA(timer_counter);
    u16 timeout = GET_BDA(com_timeout[regs->dx]);
    while (((inb(addr+SEROFF_LSR) & 0x01) == 0) && (timeout)) {
        u16 val16 = GET_BDA(timer_counter);
        if (val16 != timer) {
            timer = val16;
            timeout--;
        }
    }
    if (timeout) {
        regs->ah = 0;
        regs->al = inb(addr+SEROFF_DATA);
    } else {
        regs->ah = inb(addr+SEROFF_LSR);
    }
    set_success(regs);
}

// SERIAL - GET PORT STATUS
static void
handle_1403(struct bregs *regs)
{
    u16 addr = getComAddr(regs);
    if (!addr)
        return;
    regs->ah = inb(addr+SEROFF_LSR);
    regs->al = inb(addr+SEROFF_MSR);
    set_success(regs);
}

static void
handle_14XX(struct bregs *regs)
{
    // Unsupported
    set_fail(regs);
}

// INT 14h Serial Communications Service Entry Point
void VISIBLE16
handle_14(struct bregs *regs)
{
    debug_enter(regs, DEBUG_HDL_14);
    if (! CONFIG_SERIAL) {
        handle_14XX(regs);
        return;
    }

    irq_enable();

    switch (regs->ah) {
    case 0x00: handle_1400(regs); break;
    case 0x01: handle_1401(regs); break;
    case 0x02: handle_1402(regs); break;
    case 0x03: handle_1403(regs); break;
    default:   handle_14XX(regs); break;
    }
}

// XXX - Baud Rate Generator Table
u8 BaudTable[16] VAR16FIXED(0xe729);


/****************************************************************
 * LPT ports
 ****************************************************************/

static u16
detect_parport(u16 port, u8 timeout, u8 count)
{
    // clear input mode
    outb(inb(port+2) & 0xdf, port+2);

    outb(0xaa, port);
    if (inb(port) != 0xaa)
        // Not present
        return 0;
    SET_BDA(port_lpt[count], port);
    SET_BDA(lpt_timeout[count], timeout);
    return 1;
}

void
lpt_setup()
{
    if (! CONFIG_LPT)
        return;
    dprintf(3, "init lpt\n");

    u16 count = 0;
    count += detect_parport(0x378, 0x14, count);
    count += detect_parport(0x278, 0x14, count);
    dprintf(1, "Found %d lpt ports\n", count);

    // Equipment word bits 14..15 determing # parallel ports
    u16 eqb = GET_BDA(equipment_list_flags);
    SET_BDA(equipment_list_flags, (eqb & 0x3fff) | (count << 14));
}

static u16
getLptAddr(struct bregs *regs)
{
    if (regs->dx >= 3) {
        set_fail(regs);
        return 0;
    }
    u16 addr = GET_BDA(port_lpt[regs->dx]);
    if (! addr)
        set_fail(regs);
    return addr;
}

static void
lpt_ret(struct bregs *regs, u16 addr, u16 timeout)
{
    u8 val8 = inb(addr+1);
    regs->ah = (val8 ^ 0x48);
    if (!timeout)
        regs->ah |= 0x01;
    set_success(regs);
}

// INT 17 - PRINTER - WRITE CHARACTER
static void
handle_1700(struct bregs *regs)
{
    u16 addr = getLptAddr(regs);
    if (!addr)
        return;
    u16 timeout = GET_BDA(lpt_timeout[regs->dx]) << 8;

    outb(regs->al, addr);
    u8 val8 = inb(addr+2);
    outb(val8 | 0x01, addr+2); // send strobe
    nop();
    outb(val8 & ~0x01, addr+2);
    // XXX - implement better timeout code.
    while (((inb(addr+1) & 0x40) == 0x40) && (timeout))
        timeout--;

    lpt_ret(regs, addr, timeout);
}

// INT 17 - PRINTER - INITIALIZE PORT
static void
handle_1701(struct bregs *regs)
{
    u16 addr = getLptAddr(regs);
    if (!addr)
        return;
    u16 timeout = GET_BDA(lpt_timeout[regs->dx]) << 8;

    u8 val8 = inb(addr+2);
    outb(val8 & ~0x04, addr+2); // send init
    nop();
    outb(val8 | 0x04, addr+2);

    lpt_ret(regs, addr, timeout);
}

// INT 17 - PRINTER - GET STATUS
static void
handle_1702(struct bregs *regs)
{
    u16 addr = getLptAddr(regs);
    if (!addr)
        return;
    u16 timeout = GET_BDA(lpt_timeout[regs->dx]) << 8;

    lpt_ret(regs, addr, timeout);
}

static void
handle_17XX(struct bregs *regs)
{
    // Unsupported
    set_fail(regs);
}

// INT17h : Printer Service Entry Point
void VISIBLE16
handle_17(struct bregs *regs)
{
    debug_enter(regs, DEBUG_HDL_17);
    if (! CONFIG_LPT) {
        handle_17XX(regs);
        return;
    }

    irq_enable();

    switch (regs->ah) {
    case 0x00: handle_1700(regs); break;
    case 0x01: handle_1701(regs); break;
    case 0x02: handle_1702(regs); break;
    default:   handle_17XX(regs); break;
    }
}
