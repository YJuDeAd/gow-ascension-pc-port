/*
 * psn_stubs.cpp — Offline PlayStation Network & Online Module Stubs for God of War: Ascension.
 *
 * God of War: Ascension contains multiplayer and online PSN/network modules (sceNp, sceNp2,
 * sceNpCommerce2, sceNpTus, sceNpTrophy, cellVoice, cellSysutilNpEula, cellHttp, cellHttpUtil,
 * cellNetCtl, cellSsl, sys_net). Because official PS3 servers are offline, all network/multiplayer
 * calls are cleanly stubbed to return CELL_OK (0) and provide mock parameters so the game's
 * offline single-player campaign proceeds uninterrupted.
 */

#include "ppu_recomp.h"
#include <stdint.h>
#include <stdio.h>

extern "C" {
void     ps3_hle_register_ctx(uint32_t nid, const char* name, void (*fn)(ppu_context*));
uint32_t vm_read32(uint64_t a);
void     vm_write32(uint64_t a, uint32_t v);
}

/* Generic stub that returns CELL_OK (0) */
static void psn_stub_ok(ppu_context* ctx)
{
    ctx->gpr[3] = 0; // CELL_OK
}

/* sceNpManagerGetAccountRegion (0xB1E0718B)
 * r3 = userId, r4 = guest pointer to u32 region
 * Set region = 0x5553 ('US') and return CELL_OK */
static void stub_sceNpManagerGetAccountRegion(ppu_context* ctx)
{
    uint32_t pRegion = (uint32_t)ctx->gpr[4];
    if (pRegion) {
        vm_write32(pRegion, 0x5553u); // 'US'
    }
    ctx->gpr[3] = 0; // CELL_OK
}

/* sceNpTusCreateTitleCtx (0x7CAF58EE)
 * r3 = userId, r4 = guest pointer to titleCtx
 * Set titleCtx = 1 and return CELL_OK */
static void stub_sceNpTusCreateTitleCtx(ppu_context* ctx)
{
    uint32_t pCtx = (uint32_t)ctx->gpr[4];
    if (pCtx) {
        vm_write32(pCtx, 1);
    }
    ctx->gpr[3] = 0;
}

/* sceNpTusCreateTransactionCtx (0x1904435E)
 * r3 = titleCtx, r4 = guest pointer to transCtx
 * Set transCtx = 1 and return CELL_OK */
static void stub_sceNpTusCreateTransactionCtx(ppu_context* ctx)
{
    uint32_t pTrans = (uint32_t)ctx->gpr[4];
    if (pTrans) {
        vm_write32(pTrans, 1);
    }
    ctx->gpr[3] = 0;
}

/* cellNetCtlGetState (0x8B3EBA69)
 * r3 = pointer to int state
 * Return CELL_NET_CTL_STATE_IP_OBTAINED (3) */
static void stub_cellNetCtlGetState(ppu_context* ctx)
{
    uint32_t pState = (uint32_t)ctx->gpr[3];
    if (pState) {
        vm_write32(pState, 3); // CELL_NET_CTL_STATE_IP_OBTAINED
    }
    ctx->gpr[3] = 0; // CELL_OK
}

/* Register all PSN and multiplayer/online stubs */
extern "C" void gow_psn_stubs_register(void)
{
    /* sceNp / NP Manager (13 stubs) */
    ps3_hle_register_ctx(0x0968AA36u, "sceNpManagerGetTicket",               psn_stub_ok);
    ps3_hle_register_ctx(0x276C72B2u, "sceNp_276C72B2",                     psn_stub_ok);
    ps3_hle_register_ctx(0x50B86D94u, "sceNpSignalingAddExtendedHandler",    psn_stub_ok);
    ps3_hle_register_ctx(0x58FA4FCDu, "sceNpManagerGetTicketParam",          psn_stub_ok);
    ps3_hle_register_ctx(0x5E849303u, "sceNpBasicSetPresenceDetails2",       psn_stub_ok);
    ps3_hle_register_ctx(0x8297F1ECu, "sceNpManagerRequestTicket2",          psn_stub_ok);
    ps3_hle_register_ctx(0x860B1756u, "sceNpLookupTitleSmallStorageAsync",   psn_stub_ok);
    ps3_hle_register_ctx(0xA1709ABDu, "sceNpManagerGetEntitlementById",      psn_stub_ok);
    ps3_hle_register_ctx(0xB1E0718Bu, "sceNpManagerGetAccountRegion",        stub_sceNpManagerGetAccountRegion);
    ps3_hle_register_ctx(0xB66D1C46u, "sceNpManagerGetEntitlementIdList",    psn_stub_ok);
    ps3_hle_register_ctx(0xBDC07FD5u, "sceNpManagerGetNetworkTime",          psn_stub_ok);
    ps3_hle_register_ctx(0xE6C8F3F9u, "sceNpDrmProcessExitSpawn2",           psn_stub_ok);
    ps3_hle_register_ctx(0xFD39AE13u, "sceNp_FD39AE13",                     psn_stub_ok);

    /* sceNp2 (2 stubs) */
    ps3_hle_register_ctx(0x41251F74u, "sceNp2Init",                          psn_stub_ok);
    ps3_hle_register_ctx(0xAADB7C12u, "sceNp2Term",                          psn_stub_ok);

    /* sceNpCommerce2 (1 stub) */
    ps3_hle_register_ctx(0xEEF51BE0u, "sceNpCommerce2ExecuteStoreBrowse",    psn_stub_ok);

    /* sceNpTus (7 stubs) */
    ps3_hle_register_ctx(0x1904435Eu, "sceNpTusCreateTransactionCtx",        stub_sceNpTusCreateTransactionCtx);
    ps3_hle_register_ctx(0x2E162A62u, "sceNpTusDestroyTitleCtx",             psn_stub_ok);
    ps3_hle_register_ctx(0x325C6284u, "sceNpTus_325C6284",                   psn_stub_ok);
    ps3_hle_register_ctx(0x44ECA8B4u, "sceNpTusDestroyTransactionCtx",       psn_stub_ok);
    ps3_hle_register_ctx(0x5175ABB9u, "sceNpTusGetDataAsync",                psn_stub_ok);
    ps3_hle_register_ctx(0x7CAF58EEu, "sceNpTusCreateTitleCtx",              stub_sceNpTusCreateTitleCtx);
    ps3_hle_register_ctx(0xE847341Fu, "sceNpTusSetDataAsync",                psn_stub_ok);

    /* sceNpTrophy (1 stub) */
    ps3_hle_register_ctx(0x48BD97C7u, "sceNpTrophyAbortHandle",              psn_stub_ok);

    /* cellVoice (4 stubs) */
    ps3_hle_register_ctx(0x2A01013Eu, "cellVoice_2A01013E",                  psn_stub_ok);
    ps3_hle_register_ctx(0x35D84910u, "cellVoice_35D84910",                  psn_stub_ok);
    ps3_hle_register_ctx(0xDD000886u, "cellVoice_DD000886",                  psn_stub_ok);
    ps3_hle_register_ctx(0xFF0FA43Au, "cellVoice_FF0FA43A",                  psn_stub_ok);

    /* cellSysutilNpEula (3 stubs) */
    ps3_hle_register_ctx(0x59D1629Au, "cellSysutilNpEula_59D1629A",          psn_stub_ok);
    ps3_hle_register_ctx(0x5EC05AD8u, "cellSysutilNpEula_5EC05AD8",          psn_stub_ok);
    ps3_hle_register_ctx(0x6599500Du, "cellSysutilNpEula_6599500D",          psn_stub_ok);

    /* cellSysutilAvconfExt (1 stub) */
    ps3_hle_register_ctx(0xFAA275A4u, "cellSysutilAvconfExt_FAA275A4",       psn_stub_ok);

    /* cellNetCtl (6 stubs) */
    ps3_hle_register_ctx(0x8B3EBA69u, "cellNetCtlGetState",                  stub_cellNetCtlGetState);
    ps3_hle_register_ctx(0x0791015Fu, "cellNetCtl_0791015F",                 psn_stub_ok);
    ps3_hle_register_ctx(0x10DAE56Du, "cellNetCtl_10DAE56D",                 psn_stub_ok);
    ps3_hle_register_ctx(0x3E359AB6u, "cellNetCtl_3E359AB6",                 psn_stub_ok);
    ps3_hle_register_ctx(0x558700F6u, "cellNetCtl_558700F6",                 psn_stub_ok);
    ps3_hle_register_ctx(0x99AB1A26u, "cellNetCtl_99AB1A26",                 psn_stub_ok);
    ps3_hle_register_ctx(0xF463981Cu, "cellNetCtl_F463981C",                 psn_stub_ok);

    /* cellHttp & cellHttpUtil (8 stubs) */
    ps3_hle_register_ctx(0x1395D8D1u, "cellHttp_1395D8D1",                   psn_stub_ok);
    ps3_hle_register_ctx(0x434419C8u, "cellHttp_434419C8",                   psn_stub_ok);
    ps3_hle_register_ctx(0x54F2A4DEu, "cellHttp_54F2A4DE",                   psn_stub_ok);
    ps3_hle_register_ctx(0x61B2BADEu, "cellHttpEndCookie",                   psn_stub_ok);
    ps3_hle_register_ctx(0x9638F766u, "cellHttpInitCookie",                  psn_stub_ok);
    ps3_hle_register_ctx(0xAF73A64Eu, "cellHttp_AF73A64E",                   psn_stub_ok);
    ps3_hle_register_ctx(0xB6FEB84Bu, "cellHttp_B6FEB84B",                   psn_stub_ok);
    ps3_hle_register_ctx(0x83FAA354u, "cellHttpUtil_83FAA354",               psn_stub_ok);

    /* cellSsl (3 stubs) */
    ps3_hle_register_ctx(0x006C4900u, "cellSsl_006C4900",                    psn_stub_ok);
    ps3_hle_register_ctx(0x5E9253CAu, "cellSsl_5E9253CA",                    psn_stub_ok);
    ps3_hle_register_ctx(0x766D3CA1u, "cellSsl_766D3CA1",                    psn_stub_ok);

    /* sys_net (11 stubs) */
    ps3_hle_register_ctx(0x64F66D35u, "connect",                             psn_stub_ok);
    ps3_hle_register_ctx(0x8AF3825Eu, "sys_net_8AF3825E",                    psn_stub_ok);
    ps3_hle_register_ctx(0x8CCF05EDu, "sys_net_abort_resolver",              psn_stub_ok);
    ps3_hle_register_ctx(0x8D1B77FBu, "sys_net_abort_socket",                psn_stub_ok);
    ps3_hle_register_ctx(0xA50777C6u, "shutdown",                            psn_stub_ok);
    ps3_hle_register_ctx(0xA9A079E0u, "sys_net_A9A079E0",                    psn_stub_ok);
    ps3_hle_register_ctx(0xC9157D30u, "_sys_net_h_errno_loc",                psn_stub_ok);
    ps3_hle_register_ctx(0xC98A3146u, "sys_net_C98A3146",                    psn_stub_ok);
    ps3_hle_register_ctx(0xDC751B40u, "send",                                psn_stub_ok);
    ps3_hle_register_ctx(0x5A045BD1u, "getsockopt",                          psn_stub_ok);
    ps3_hle_register_ctx(0xDABBC2C0u, "inet_addr",                           psn_stub_ok);
}
