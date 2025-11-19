/*******************************************************************************
 * PolarFire SoC - Receiver Node (End-to-End Integration)
 *
 * Merges:
 * 1. nRF24L01 Receiver (from rx1-10s.txt)
 * 2. SPI OLED Display (from SPI OLED.txt)
 *
 * Hardware Configuration (Based on PDF Fig 9 and User Request):
 * - OLED Display: Uses SPI0 (&g_mss_spi0_lo)
 * - nRF Module:   Uses SPI1 (&g_mss_spi1_lo)
 *
 * This code receives an 8-byte payload (int32_t T, int32_t RH)
 * and displays the formatted values on the OLED screen.
 ******************************************************************************/

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "mpfs_hal/mss_hal.h"
#include "drivers/mss/mss_spi/mss_spi.h"
#include "drivers/mss/mss_mmuart/mss_uart.h"
#include "drivers/mss/mss_gpio/mss_gpio.h"
#include "inc/uart_mapping.h"

/* ===== Boot handoff (from rx1-10s.txt) ===== */
#ifndef IMAGE_LOADED_BY_BOOTLOADER
#define IMAGE_LOADED_BY_BOOTLOADER 0
#endif
#if (IMAGE_LOADED_BY_BOOTLOADER == 0)
static inline void wait_for_boot(void){
    clear_soft_interrupt(); set_csr(mie, MIP_MSIP);
    do { __asm__("wfi"); } while ((read_csr(mip) & MIP_MSIP) == 0);
    clear_soft_interrupt();
}
#else
static inline void wait_for_boot(void){}
#endif

/* ===== UART (from rx1-10s.txt) ===== */
extern struct mss_uart_instance* p_uartmap_u54_1;
static mss_uart_instance_t* g_uart = 0;
static inline void LOG(const char* s){
    if(g_uart) MSS_UART_polled_tx_string(g_uart, (const uint8_t*)s);
}

/* ===== Delays (from rx1-10s.txt) ===== */
static void delay_us(uint32_t us){ volatile uint32_t i; while(us--) for(i=0;i<50;++i) __asm__ volatile("nop"); }
static void delay_ms(uint32_t ms){ while(ms--) delay_us(1000); }


/* ============================================================================
 * OLED Driver (on SPI0) - From "SPI OLED.txt"
 * ==========================================================================*/

/* ---- OLED Visible area ---- */
#define OLED_W      96u
#define OLED_H      96u
#define COL_START   16u
#define ROW_START    0u
#define COL_END     (COL_START + OLED_W - 1)
#define ROW_END     (ROW_START + OLED_H - 1)

/* ---- OLED Pins (Modified to match PDF Figure 9) ---- */
#define OLED_GPIO_PORT GPIO1_LO
#define OLED_EN_PIN  3u   /* J6-2 (INT) -> GPIO1_3 */
#define OLED_DC_PIN  17u  /* J10-11 -> GPIO_17 */

/* ---- OLED SPI (SPI0) ---- */
#define OLED_SPIx       (&g_mss_spi0_lo) /* Using SPI0 for OLED */
#define OLED_SPI_SS     MSS_SPI_SLAVE_0
#define OLED_SPI_MODE   MSS_SPI_MODE3
#define OLED_SPI_DIV    128u

/* ---- OLED helpers (renamed to avoid conflicts) ---- */
static void oled_spi_overflow_handler(uint8_t core){ (void)core; }
static inline void oled_dc_cmd(void){  MSS_GPIO_set_output(OLED_GPIO_PORT, OLED_DC_PIN, 0u); }
static inline void oled_dc_data(void){ MSS_GPIO_set_output(OLED_GPIO_PORT, OLED_DC_PIN, 1u); }
static inline void oled_cs_low(void){  MSS_SPI_set_slave_select(OLED_SPIx, OLED_SPI_SS); }
static inline void oled_cs_high(void){ MSS_SPI_clear_slave_select(OLED_SPIx, OLED_SPI_SS); }
static inline void oled_spi_tx(const uint8_t* d, uint32_t n){
    MSS_SPI_transfer_block(OLED_SPIx, d, n, 0, 0);
}

static void ssd_cmd(uint8_t c){ oled_cs_low(); oled_dc_cmd(); oled_spi_tx(&c,1); oled_cs_high(); }
static void ssd_cmd_data(uint8_t cmd, const uint8_t* d, uint32_t n){
    oled_cs_low(); oled_dc_cmd(); oled_spi_tx(&cmd,1);
    if(n){ oled_dc_data(); oled_spi_tx(d,n); }
    oled_cs_high();
}

static void panel_power_up(void){
    MSS_GPIO_set_output(OLED_GPIO_PORT, OLED_EN_PIN, 1u);
    delay_ms(120);
}

static void ssd_init_96_centered(void)
{
    { uint8_t v=0x12; ssd_cmd_data(0xFD,&v,1); }
    { uint8_t v=0xB1; ssd_cmd_data(0xFD,&v,1); }
    ssd_cmd(0xAE);
    { uint8_t v=0x76; ssd_cmd_data(0xA0,&v,1); }
    { uint8_t v=0x00; ssd_cmd_data(0xA1,&v,1); }
    { uint8_t v=0x10; ssd_cmd_data(0xA2,&v,1); }
    { uint8_t v=0x80; ssd_cmd_data(0xB3,&v,1); }
    { uint8_t v=0x5F; ssd_cmd_data(0xCA,&v,1); }
    { uint8_t c[3]={0xC8,0x80,0xC8}; ssd_cmd_data(0xC1,c,3); }
    { uint8_t v=0x0F; ssd_cmd_data(0xC7,&v,1); }
    { uint8_t v=0x32; ssd_cmd_data(0xB1,&v,1); }
    { uint8_t v=0x05; ssd_cmd_data(0xBE,&v,1); }
    ssd_cmd(0xA6);
    ssd_cmd(0xAF);
}

static void ssd_set_full_window(void){
    uint8_t col[2] = { COL_START, COL_END };
    uint8_t row[2] = { ROW_START, ROW_END };
    ssd_cmd_data(0x15, col, 2);
    ssd_cmd_data(0x75, row, 2);
}

static void ssd_write_color_repeat(uint16_t rgb565, uint32_t count){
    uint8_t hi = (uint8_t)(rgb565 >> 8), lo = (uint8_t)(rgb565 & 0xFF);
    uint8_t burst[128*2];
    for (uint32_t i=0;i<sizeof(burst); i+=2){ burst[i]=hi; burst[i+1]=lo; }

    oled_cs_low();
    oled_dc_cmd(); { uint8_t wr=0x5C; oled_spi_tx(&wr,1); }
    oled_dc_data();
    while(count){
        uint32_t n = (count>128u)?128u:count;
        oled_spi_tx(burst, n*2u);
        count -= n;
    }
    oled_cs_high();
}

static void ssd_clear_full128(void){
    uint8_t col[2] = {0, 127}, row[2] = {0, 127};
    ssd_cmd_data(0x15, col, 2);
    ssd_cmd_data(0x75, row, 2);
    ssd_write_color_repeat(0x0000, (uint32_t)128u*128u);
}

static void ssd_fill_screen(uint16_t rgb565){
    ssd_set_full_window();
    ssd_write_color_repeat(rgb565, (uint32_t)OLED_W * (uint32_t)OLED_H);
}

/* ---- OLED Glyphs (expanded for T/RH display) ---- */
typedef struct { char ch; uint8_t col[5]; } glyph5x7_t;
static const glyph5x7_t G[] = {
    {' ', {0x00,0x00,0x00,0x00,0x00}},
    {'H', {0x7F,0x08,0x08,0x08,0x7F}},
    {'E', {0x7F,0x49,0x49,0x49,0x41}},
    {'L', {0x7F,0x40,0x40,0x40,0x40}},
    {'O', {0x3E,0x41,0x41,0x41,0x3E}},
    {'W', {0x7F,0x02,0x04,0x02,0x7F}},
    {'R', {0x7F,0x09,0x19,0x29,0x46}},
    {'D', {0x7F,0x41,0x41,0x22,0x1C}},
    {'T', {0x01,0x01,0x7F,0x01,0x01}},
    {':', {0x00,0x36,0x36,0x00,0x00}},
    {'-', {0x08,0x08,0x08,0x08,0x08}},
    {'.', {0x00,0x40,0x40,0x00,0x00}},
    {'%', {0x22,0x14,0x08,0x50,0x44}},
    {'C', {0x3E,0x41,0x41,0x41,0x22}},
    {'0', {0x3E,0x45,0x49,0x51,0x3E}},
    {'1', {0x00,0x42,0x7F,0x40,0x00}},
    {'2', {0x42,0x61,0x51,0x49,0x46}},
    {'3', {0x21,0x41,0x45,0x4B,0x31}},
    {'4', {0x18,0x14,0x12,0x7F,0x10}},
    {'5', {0x27,0x45,0x45,0x45,0x39}},
    {'6', {0x3C,0x4A,0x49,0x49,0x30}},
    {'7', {0x01,0x71,0x09,0x05,0x03}},
    {'8', {0x36,0x49,0x49,0x49,0x36}},
    {'9', {0x06,0x49,0x49,0x29,0x1E}},
};
static const uint8_t* glyph_cols(char c){
    for (unsigned i=0;i<sizeof(G)/sizeof(G[0]);++i) if (G[i].ch==c) return G[i].col;
    return G[0].col; /* space if missing */
}

static inline void ssd_putpix(uint8_t x,uint8_t y,uint16_t rgb565){
    uint8_t hi=rgb565>>8, lo=rgb565&0xFF;
    uint8_t col[2]={ (uint8_t)(COL_START+x), (uint8_t)(COL_START+x) };
    uint8_t row[2]={ (uint8_t)(ROW_START+y), (uint8_t)(ROW_START+y) };
    ssd_cmd_data(0x15,col,2);
    ssd_cmd_data(0x75,row,2);
    oled_cs_low(); oled_dc_cmd(); { uint8_t wr=0x5C; oled_spi_tx(&wr,1); } oled_dc_data();
    uint8_t p[2]={hi,lo}; oled_spi_tx(p,2);
    oled_cs_high();
}

static void ssd_draw_char(uint8_t x,uint8_t y,char c,uint16_t fg,uint16_t bg){
    const uint8_t* col = glyph_cols(c);
    for (uint8_t cx=0; cx<6; ++cx){
        uint8_t bits = (cx<5)? col[cx] : 0x00;
        for (uint8_t cy=0; cy<7; ++cy){
            uint16_t color = (bits & 0x01) ? fg : bg;
            if(x+cx < OLED_W && y+cy < OLED_H) {
                 ssd_putpix(x+cx, y+cy, color);
            }
            bits >>= 1;
        }
    }
}

static void ssd_draw_string(uint8_t x,uint8_t y,const char* s,uint16_t fg,uint16_t bg){
    while (*s && x+6 <= OLED_W){
        ssd_draw_char(x,y,*s,fg,bg);
        x += 6;
        ++s;
    }
}


/* ============================================================================
 * nRF24L01 Driver (on SPI1) - From "rx1-10s.txt"
 * ==========================================================================*/

/* ---- nRF SPI (MOVED TO SPI1) ---- */
#define NRF_SPIx        (&g_mss_spi1_lo)
#define NRF_SPI_DIV     128u

/* ---- nRF Pins (Modified to match PDF Figure 9) ---- */
#define NRF_GPIO_PORT  GPIO1_LO
#define NRF_CE_PIN     7u   /* J10-26 -> GPIO_7 */
#define NRF_IRQ_PIN    4u   /* J10-7  -> GPIO_4 */

static inline void nrf_ce_low (void){ MSS_GPIO_set_output(NRF_GPIO_PORT, NRF_CE_PIN, 0u); }
static inline void nrf_ce_high(void){ MSS_GPIO_set_output(NRF_GPIO_PORT, NRF_CE_PIN, 1u); }

/* ---- nRF defs (renamed to avoid conflicts) ---- */
#define CMD_R_REGISTER_NRF      0x00
#define CMD_W_REGISTER_NRF      0x20
#define CMD_R_RX_PAYLOAD_NRF    0x61
#define CMD_FLUSH_RX_NRF        0xE2
#define CMD_NOP_NRF             0xFF
#define REG_CONFIG_NRF          0x00
#define REG_EN_AA_NRF           0x01
#define REG_EN_RXADDR_NRF       0x02
#define REG_SETUP_AW_NRF        0x03
#define REG_RF_CH_NRF           0x05
#define REG_RF_SETUP_NRF        0x06
#define REG_STATUS_NRF          0x07
#define REG_RX_ADDR_P0_NRF      0x0A
#define REG_RX_PW_P0_NRF        0x11
#define REG_FIFO_STATUS_NRF     0x17
#define REG_CD_NRF              0x09
#define EN_CRC_NRF  (1u<<3)
#define PWR_UP_NRF  (1u<<1)
#define PRIM_RX_NRF (1u<<0)
#define RX_DR_NRF   (1u<<6)
#define TX_DS_NRF   (1u<<5)
#define MAX_RT_NRF  (1u<<4)
#define ADDR_LEN_NRF     5
#define PAYLOAD_LEN_NRF  8 /* 4B T_centi + 4B RH_centi */

/* ---- nRF SPI helpers (renamed to avoid conflicts) ---- */
static void nrf_spi_overflow_handler(uint8_t core){ (void)core; }
static mss_spi_slave_t nrf_g_ss  = MSS_SPI_SLAVE_0;
static mss_spi_protocol_mode_t nrf_g_mode = MSS_SPI_MODE0;

static inline void nrf_spi_cfg_for(mss_spi_slave_t ss, mss_spi_protocol_mode_t mode){
    MSS_SPI_configure_master_mode(NRF_SPIx, ss, mode, NRF_SPI_DIV,
                                  MSS_SPI_BLOCK_TRANSFER_FRAME_SIZE,
                                  nrf_spi_overflow_handler);
}
static inline void nrf_spi_set_ss(mss_spi_slave_t ss){ MSS_SPI_set_slave_select(NRF_SPIx, ss); }
static inline void nrf_spi_clr_ss(mss_spi_slave_t ss){ MSS_SPI_clear_slave_select(NRF_SPIx, ss); }

static void nrf_spi_xfer_sel(mss_spi_slave_t ss, const uint8_t* tx, uint8_t* rx, uint32_t len){
    nrf_spi_set_ss(ss);
    MSS_SPI_transfer_block(NRF_SPIx, tx, len, rx, len);
    nrf_spi_clr_ss(ss);
}
static void nrf_spi_write_sel(mss_spi_slave_t ss, const uint8_t* tx, uint32_t len){
    nrf_spi_set_ss(ss);
    MSS_SPI_transfer_block(NRF_SPIx, tx, len, 0, 0);
    nrf_spi_clr_ss(ss);
}

/* ---- nRF ops (using nrf_spi_... helpers) ---- */
static uint8_t nrf_read_reg(uint8_t reg){
    uint8_t t[2]={(uint8_t)(CMD_R_REGISTER_NRF|(reg&0x1F)), CMD_NOP_NRF}, r[2]={0};
    nrf_spi_xfer_sel(nrf_g_ss,t,r,2); return r[1];
}
static void nrf_write_reg(uint8_t reg, uint8_t val){
    uint8_t t[2]={(uint8_t)(CMD_W_REGISTER_NRF|(reg&0x1F)), val};
    nrf_spi_write_sel(nrf_g_ss,t,2);
}
static void nrf_write_buf(uint8_t reg, const uint8_t *buf, uint32_t len){
    uint8_t hdr=(uint8_t)(CMD_W_REGISTER_NRF|(reg&0x1F));
    nrf_spi_set_ss(nrf_g_ss);
    MSS_SPI_transfer_block(NRF_SPIx,&hdr,1,NULL,0);
    MSS_SPI_transfer_block(NRF_SPIx,buf,len,NULL,0);
    nrf_spi_clr_ss(nrf_g_ss);
}
static void nrf_clear_irqs(void){ nrf_write_reg(REG_STATUS_NRF, RX_DR_NRF|TX_DS_NRF|MAX_RT_NRF); }
static void nrf_flush_rx(void){ uint8_t c=CMD_FLUSH_RX_NRF; nrf_spi_write_sel(nrf_g_ss,&c,1); }
static void nrf_set_channel(uint8_t ch){ nrf_write_reg(REG_RF_CH_NRF, (uint8_t)(ch & 0x7F)); }
static void nrf_set_rf_1M_0dBm(void){ nrf_write_reg(REG_RF_SETUP_NRF, 0x07); }

static uint8_t nrf_rx_fifo_empty(void){
    uint8_t f = nrf_read_reg(REG_FIFO_STATUS_NRF);
    return (f & 0x01u) != 0;
}

static void nrf_read_rx_payload(uint8_t *out, uint8_t len){
    if(len==0) return; if(len>32) len=32;
    uint8_t tx[1+32]={0};
    uint8_t rx[1+32]={0};
    tx[0] = CMD_R_RX_PAYLOAD_NRF;
    for (uint8_t i=1;i<=len;++i) tx[i] = CMD_NOP_NRF;
    nrf_spi_xfer_sel(nrf_g_ss, tx, rx, 1u+len);
    for (uint8_t i=0;i<len;++i) out[i] = rx[1+i];
}

static void nrf_dump_regs(void){
    char line[128];
    uint8_t config=nrf_read_reg(REG_CONFIG_NRF);
    uint8_t rfsetup=nrf_read_reg(REG_RF_SETUP_NRF);
    uint8_t rfch   =nrf_read_reg(REG_RF_CH_NRF);
    uint8_t enaa   =nrf_read_reg(REG_EN_AA_NRF);
    uint8_t enrx   =nrf_read_reg(REG_EN_RXADDR_NRF);
    uint8_t aw     =nrf_read_reg(REG_SETUP_AW_NRF);
    uint8_t rxpw0  =nrf_read_reg(REG_RX_PW_P0_NRF);
    snprintf(line,sizeof line,
      "[RADIO][RX] CFG=0x%02X RF_SETUP=0x%02X CH=%u EN_AA=0x%02X EN_RXADDR=0x%02X AW=0x%02X RX_PW_P0=%u\r\n",
      config, rfsetup, rfch, enaa, enrx, aw, rxpw0);
    LOG(line);
}

/* ---- nRF Auto-probe (Modified to use NRF_SPIx) ---- */
static uint8_t nrf_try_rw_ch_cfg(mss_spi_slave_t ss, mss_spi_protocol_mode_t mode, uint8_t* out_ch, uint8_t* out_cfg){
    nrf_spi_cfg_for(ss, mode);
    uint8_t tx1[2]={(uint8_t)(CMD_W_REGISTER_NRF|REG_RF_CH_NRF), 76};  nrf_spi_write_sel(ss, tx1, 2);
    uint8_t tx2[2]={(uint8_t)(CMD_R_REGISTER_NRF|REG_RF_CH_NRF), CMD_NOP_NRF}, rx2[2]={0}; nrf_spi_xfer_sel(ss, tx2, rx2, 2);
    uint8_t tx3[2]={(uint8_t)(CMD_W_REGISTER_NRF|REG_CONFIG_NRF), (uint8_t)(EN_CRC_NRF|PWR_UP_NRF|PRIM_RX_NRF)};
    nrf_spi_write_sel(ss, tx3, 2);
    uint8_t tx4[2]={(uint8_t)(CMD_R_REGISTER_NRF|REG_CONFIG_NRF), CMD_NOP_NRF}, rx4[2]={0}; nrf_spi_xfer_sel(ss, tx4, rx4, 2);
    *out_ch = rx2[1]; *out_cfg = rx4[1];
    return (rx2[1] == 76u) && ((rx4[1] & PWR_UP_NRF) != 0);
}
static uint8_t nrf_probe_cs_mode(void){
    mss_spi_slave_t ss_opts[] = { MSS_SPI_SLAVE_0, MSS_SPI_SLAVE_1 };
    mss_spi_protocol_mode_t md_opts[] = { MSS_SPI_MODE0, MSS_SPI_MODE3 };
    char line[96];
    for (unsigned i=0;i<sizeof(ss_opts)/sizeof(ss_opts[0]);++i){
        for (unsigned j=0;j<sizeof(md_opts)/sizeof(md_opts[0]);++j){
            uint8_t ch=0, cfg=0;
            uint8_t ok = nrf_try_rw_ch_cfg(ss_opts[i], md_opts[j], &ch, &cfg);
            snprintf(line,sizeof line,"[RX] SPI1 probe SS=%d MODE=%d -> CH=%u CFG=0x%02X\r\n",
                     (int)ss_opts[i], (int)md_opts[j], ch, cfg);
            LOG(line);
            if (ok){
                nrf_g_ss = ss_opts[i]; nrf_g_mode = md_opts[j];
                nrf_spi_cfg_for(nrf_g_ss, nrf_g_mode);
                snprintf(line,sizeof line,"[RX] Using SPI1 SS=%d MODE=%d\r\n",(int)nrf_g_ss,(int)nrf_g_mode);
                LOG(line);
                return 1u;
            }
        }
    }
    return 0u;
}


/* ============================================================================
 * Main Hart Entry (U54_1) - MERGED
 * ==========================================================================*/
void u54_1(void)
{
    wait_for_boot();

    /* Clocks + fabric */
    mss_config_clk_rst(MSS_PERIPH_MMUART_U54_1, 1, PERIPHERAL_ON);
    mss_config_clk_rst(MSS_PERIPH_SPI0,         1, PERIPHERAL_ON); /* For OLED */
    mss_config_clk_rst(MSS_PERIPH_SPI1,         1, PERIPHERAL_ON); /* For nRF */
    mss_config_clk_rst(MSS_PERIPH_GPIO1,        1, PERIPHERAL_ON);
    mss_enable_fabric();

    /* UART */
    g_uart = p_uartmap_u54_1;
    MSS_UART_init(g_uart, MSS_UART_115200_BAUD,
                  MSS_UART_DATA_8_BITS | MSS_UART_NO_PARITY | MSS_UART_ONE_STOP_BIT);
    LOG("\r\n[RX NODE] BOOT\r\n");

    /* GPIO */
    MSS_GPIO_init(GPIO1_LO);
    /* OLED Pins (from PDF) */
    MSS_GPIO_config(OLED_GPIO_PORT, OLED_EN_PIN, MSS_GPIO_OUTPUT_MODE);
    MSS_GPIO_config(OLED_GPIO_PORT, OLED_DC_PIN, MSS_GPIO_OUTPUT_MODE);
    MSS_GPIO_set_output(OLED_GPIO_PORT, OLED_EN_PIN, 1u); /* Default ON */
    MSS_GPIO_set_output(OLED_GPIO_PORT, OLED_DC_PIN, 1u); /* Default DATA */
    /* nRF Pins (from PDF) */
    MSS_GPIO_config(NRF_GPIO_PORT, NRF_CE_PIN,  MSS_GPIO_OUTPUT_MODE);
    MSS_GPIO_config(NRF_GPIO_PORT, NRF_IRQ_PIN, MSS_GPIO_INPUT_MODE);
    nrf_ce_low();
    LOG("[RX] GPIO Configured (OLED on SPI0, nRF on SPI1 pins)\r\n");

    /* SPI0 (OLED) Init */
    MSS_SPI_init(OLED_SPIx);
    MSS_SPI_configure_master_mode(OLED_SPIx, OLED_SPI_SS, OLED_SPI_MODE, OLED_SPI_DIV,
                                  MSS_SPI_BLOCK_TRANSFER_FRAME_SIZE,
                                  oled_spi_overflow_handler);
    LOG("[RX] SPI0 for OLED Initialized.\r\n");

    /* OLED Panel Init */
    panel_power_up();
    ssd_init_96_centered();
    ssd_clear_full128();
    ssd_fill_screen(0x0000); /* Black */
    ssd_draw_string(10, 44, "WAITING...", 0xFFFF, 0x0000);
    LOG("[RX] OLED Initialized.\r\n");

    /* SPI1 (nRF) Init */
    MSS_SPI_init(NRF_SPIx);
    if (!nrf_probe_cs_mode()){
        LOG("[RX] nRF ERROR: No SS/MODE matched on SPI1.\r\n");
        for(;;){}
    }

    /* ===== nRF Packet setup (from rx1-10s.txt) ===== */
    nrf_set_channel(76);
    nrf_clear_irqs();
    nrf_flush_rx();
    nrf_set_rf_1M_0dBm();

    nrf_write_reg(REG_SETUP_AW_NRF, 0x03);
    const uint8_t addr[ADDR_LEN_NRF] = {0xE7,0xE7,0xE7,0xE7,0xE7};
    nrf_write_buf(REG_RX_ADDR_P0_NRF, addr, ADDR_LEN_NRF);
    nrf_write_reg(REG_EN_AA_NRF,     0x00);
    nrf_write_reg(REG_EN_RXADDR_NRF, 0x01);
    nrf_write_reg(REG_RX_PW_P0_NRF,  PAYLOAD_LEN_NRF);

    uint8_t cfg = EN_CRC_NRF | PWR_UP_NRF | PRIM_RX_NRF;
    nrf_write_reg(REG_CONFIG_NRF, cfg);
    delay_us(1500);
    nrf_ce_high();

    nrf_dump_regs();
    LOG("[RX] Listening @ CH=76 (minimal)\r\n");

    uint8_t buf[PAYLOAD_LEN_NRF];
    char line[160];
    char oled_line_t[16];
    char oled_line_rh[16];

    while (1){
        /* Drain RX FIFO if any (from rx1-10s.txt) */
        if (!nrf_rx_fifo_empty()){
            nrf_read_rx_payload(buf, PAYLOAD_LEN_NRF);
            nrf_clear_irqs();

            /* 1. Decode 8-byte payload (Integration step) */
            int32_t t_centi, rh_centi;
            memcpy(&t_centi, buf, sizeof(int32_t));
            memcpy(&rh_centi, buf + sizeof(int32_t), sizeof(int32_t));

            /* 2. Log to UART (Integration step) */
            int32_t t_abs = (t_centi < 0) ? -t_centi : t_centi;
            int32_t rh_abs = (rh_centi < 0) ? -rh_centi : rh_centi;

            snprintf(line, sizeof line, "[RX] T: %s%d.%02d C, RH: %s%d.%02d %%\r\n",
                     (t_centi < 0 ? "-" : ""), (int)(t_abs / 100), (int)(t_abs % 100),
                     (rh_centi < 0 ? "-" : ""), (int)(rh_abs / 100), (int)(rh_abs % 100));
            LOG(line);

            /* 3. Format strings for OLED (Integration step) */
            snprintf(oled_line_t, sizeof(oled_line_t), "T: %s%d.%02d C",
                     (t_centi < 0 ? "-" : " "), (int)(t_abs / 100), (int)(t_abs % 100));
            snprintf(oled_line_rh, sizeof(oled_line_rh), "RH: %s%d.%02d %%",
                     (rh_centi < 0 ? "-" : " "), (int)(rh_abs / 100), (int)(rh_abs % 100));

            /* 4. Display on OLED (from SPI OLED.txt) */
            ssd_fill_screen(0x0000); /* Black background */
            ssd_draw_string(10, 30, oled_line_t,  0xFFFF, 0x0000); /* White text */
            ssd_draw_string(10, 50, oled_line_rh, 0xFFFF, 0x0000); /* White text */
        }

        delay_ms(10); /* Small delay to not poll too fast */
    }
}

void Software_h1_IRQHandler(void){}