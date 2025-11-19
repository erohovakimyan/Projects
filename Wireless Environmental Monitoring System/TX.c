/*******************************************************************************
 * PolarFire SoC - Transmitter Node (End-to-End Integration)
 *
 * Merges:
 * 1. I2C temp sensor (HDC2080) on I2C0 (from I2C temp sensor.txt)
 * 2. nRF24L01 Transmitter on SPI0 (from tx1-10s.txt)
 *
 * Reads T/RH from HDC2080 and transmits via nRF24L01 every second.
 ******************************************************************************/

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include "mpfs_hal/mss_hal.h"
#include "drivers/mss/mss_spi/mss_spi.h"
#include "drivers/mss/mss_mmuart/mss_uart.h"
#include "drivers/mss/mss_gpio/mss_gpio.h"
#include "drivers/mss/mss_i2c/mss_i2c.h"
#include "inc/uart_mapping.h"

/* ===== Config toggles (from tx1-10s.txt) ===== */
#define DO_CW_TEST  0   /* 1 = emit 10s continuous carrier at boot, 0 = skip */

/* ===== Boot handoff (from tx1-10s.txt) ===== */
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

/* ===== UART (from tx1-10s.txt) ===== */
extern struct mss_uart_instance* p_uartmap_u54_1;
static mss_uart_instance_t* g_uart = 0;
static inline void LOG(const char* s){
    if(g_uart) MSS_UART_polled_tx_string(g_uart, (const uint8_t*)s);
}

/* ===== nRF24L01 (SPI0) Defs (from tx1-10s.txt) ===== */
#define NRF_SPIx        (&g_mss_spi0_lo)
#define NRF_SPI_DIV     128u
#define NRF_GPIO_PORT   GPIO1_LO
#define NRF_CE_PIN      20u
#define NRF_IRQ_PIN     MSS_GPIO_3

static inline void ce_low (void){ MSS_GPIO_set_output(NRF_GPIO_PORT, NRF_CE_PIN, 0u); }
static inline void ce_high(void){ MSS_GPIO_set_output(NRF_GPIO_PORT, NRF_CE_PIN, 1u); }

/* ===== HDC2080 (I2C0) Defs (from I2C temp sensor.txt) ===== */
#define I2C_SENSOR        (&g_mss_i2c0_lo)
#define SENSOR_PERIPH     MSS_PERIPH_I2C0
#define MASTER_SER_ADDR   0x21u
#define I2C_CLKDIV        MSS_I2C_PCLK_DIV_192
#define DEMO_I2C_TIMEOUT  3000u
static uint8_t g_dev_addr = 0x40u; /* 7-bit device address */

/* ===== Delays (from tx1-10s.txt) ===== */
static void delay_us(uint32_t us){ volatile uint32_t i; while(us--) for(i=0;i<50;++i) __asm__ volatile("nop"); }
static void delay_ms(uint32_t ms){ while(ms--) delay_us(1000); }

/* ===== nRF24L01 Register Defs (from tx1-10s.txt) ===== */
#define CMD_R_REGISTER      0x00
#define CMD_W_REGISTER      0x20
#define CMD_W_TX_PAYLOAD    0xA0
#define CMD_FLUSH_TX        0xE1
#define CMD_FLUSH_RX        0xE2
#define CMD_NOP             0xFF
#define REG_CONFIG          0x00
#define REG_EN_AA           0x01
#define REG_EN_RXADDR       0x02
#define REG_SETUP_AW        0x03
#define REG_SETUP_RETR      0x04
#define REG_RF_CH           0x05
#define REG_RF_SETUP        0x06
#define REG_STATUS          0x07
#define REG_RX_ADDR_P0      0x0A
#define REG_TX_ADDR         0x10
#define REG_RX_PW_P0        0x11
#define REG_FIFO_STATUS     0x17
#define EN_CRC  (1u<<3)
#define PWR_UP  (1u<<1)
#define PRIM_RX (1u<<0)
#define RX_DR   (1u<<6)
#define TX_DS   (1u<<5)
#define MAX_RT  (1u<<4)
#define ADDR_LEN     5
#define PAYLOAD_LEN  8      /* 4 bytes T_centi + 4 bytes RH_centi */

/* ===== nRF SPI Helpers (from tx1-10s.txt) ===== */
static void spi_overflow_handler(uint8_t core){ (void)core; }
static mss_spi_slave_t g_ss  = MSS_SPI_SLAVE_0;
static mss_spi_protocol_mode_t g_mode = MSS_SPI_MODE0;

static inline void spi_cfg_for(mss_spi_slave_t ss, mss_spi_protocol_mode_t mode){
    MSS_SPI_configure_master_mode(NRF_SPIx, ss, mode, NRF_SPI_DIV,
                                  MSS_SPI_BLOCK_TRANSFER_FRAME_SIZE,
                                  spi_overflow_handler);
}
static inline void spi_set_ss(mss_spi_slave_t ss){ MSS_SPI_set_slave_select(NRF_SPIx, ss); }
static inline void spi_clr_ss(mss_spi_slave_t ss){ MSS_SPI_clear_slave_select(NRF_SPIx, ss); }

static void spi_xfer_sel(mss_spi_slave_t ss, const uint8_t* tx, uint8_t* rx, uint32_t len){
    spi_set_ss(ss);
    MSS_SPI_transfer_block(NRF_SPIx, tx, len, rx, len);
    spi_clr_ss(ss);
}
static void spi_write_sel(mss_spi_slave_t ss, const uint8_t* tx, uint32_t len){
    spi_set_ss(ss);
    MSS_SPI_transfer_block(NRF_SPIx, tx, len, 0, 0);
    spi_clr_ss(ss);
}

/* ===== nRF Ops (from tx1-10s.txt) ===== */
static uint8_t nrf_read_reg(uint8_t reg){
    uint8_t t[2]={(uint8_t)(CMD_R_REGISTER|(reg&0x1F)), CMD_NOP}, r[2]={0};
    spi_xfer_sel(g_ss,t,r,2); return r[1];
}
static void nrf_write_reg(uint8_t reg, uint8_t val){
    uint8_t t[2]={(uint8_t)(CMD_W_REGISTER|(reg&0x1F)), val};
    spi_write_sel(g_ss,t,2);
}
static void nrf_write_buf(uint8_t reg, const uint8_t *buf, uint32_t len){
    uint8_t hdr=(uint8_t)(CMD_W_REGISTER|(reg&0x1F));
    spi_set_ss(g_ss);
    MSS_SPI_transfer_block(NRF_SPIx,&hdr,1,NULL,0);
    MSS_SPI_transfer_block(NRF_SPIx,buf,len,NULL,0);
    spi_clr_ss(g_ss);
}
static void nrf_flush_tx(void){ uint8_t c=CMD_FLUSH_TX; spi_write_sel(g_ss,&c,1); }
static void nrf_flush_rx(void){ uint8_t c=CMD_FLUSH_RX; spi_write_sel(g_ss,&c,1); }
static void nrf_clear_irqs(void){ nrf_write_reg(REG_STATUS, RX_DR|TX_DS|MAX_RT); }
static void nrf_set_channel(uint8_t ch){ nrf_write_reg(REG_RF_CH, (uint8_t)(ch & 0x7F)); }
static void nrf_set_rf_1M_0dBm(void){ nrf_write_reg(REG_RF_SETUP, 0x07); }

static void nrf_send_payload(const uint8_t *payload, uint8_t len){
    if(len==0) return;
    if(len>32) len=32;
    nrf_clear_irqs();
    ce_low();
    uint8_t cmd=CMD_W_TX_PAYLOAD;
    spi_set_ss(g_ss);
    MSS_SPI_transfer_block(NRF_SPIx,&cmd,1,NULL,0);
    MSS_SPI_transfer_block(NRF_SPIx,payload,len,NULL,0);
    spi_clr_ss(g_ss);
    ce_high(); delay_us(20); ce_low();
}

static void cw_test(void){
    LOG("[TX] CW ON @ CH=76 (2476 MHz) for 10s\r\n");
    nrf_write_reg(REG_CONFIG, EN_CRC | PWR_UP);
    delay_us(1500);
    nrf_set_channel(76);
    nrf_write_reg(REG_RF_SETUP, 0x97);
    ce_high();
    delay_ms(10000);
    ce_low();
    nrf_set_rf_1M_0dBm();
    LOG("[TX] CW OFF\r\n");
}

static void nrf_dump_regs(void){
    char line[128];
    uint8_t config=nrf_read_reg(REG_CONFIG);
    uint8_t rfsetup=nrf_read_reg(REG_RF_SETUP);
    uint8_t rfch   =nrf_read_reg(REG_RF_CH);
    uint8_t enaa   =nrf_read_reg(REG_EN_AA);
    uint8_t enrx   =nrf_read_reg(REG_EN_RXADDR);
    uint8_t aw     =nrf_read_reg(REG_SETUP_AW);
    uint8_t rxpw0  =nrf_read_reg(REG_RX_PW_P0);
    snprintf(line,sizeof line,
      "[RADIO][TX] CFG=0x%02X RF_SETUP=0x%02X CH=%u EN_AA=0x%02X EN_RXADDR=0x%02X AW=0x%02X RX_PW_P0=%u\r\n",
      config, rfsetup, rfch, enaa, enrx, aw, rxpw0);
    LOG(line);
}

/* ===== nRF Auto-probe SS/MODE (from tx1-10s.txt) ===== */
static uint8_t try_rw_ch_cfg(mss_spi_slave_t ss, mss_spi_protocol_mode_t mode, uint8_t* out_ch, uint8_t* out_cfg){
    spi_cfg_for(ss, mode);
    uint8_t tx1[2]={(uint8_t)(CMD_W_REGISTER|REG_RF_CH), 76};  spi_write_sel(ss, tx1, 2);
    uint8_t tx2[2]={(uint8_t)(CMD_R_REGISTER|REG_RF_CH), CMD_NOP}, rx2[2]={0}; spi_xfer_sel(ss, tx2, rx2, 2);
    uint8_t tx3[2]={(uint8_t)(CMD_W_REGISTER|REG_CONFIG), (uint8_t)(EN_CRC|PWR_UP)};
    spi_write_sel(ss, tx3, 2);
    uint8_t tx4[2]={(uint8_t)(CMD_R_REGISTER|REG_CONFIG), CMD_NOP}, rx4[2]={0}; spi_xfer_sel(ss, tx4, rx4, 2);
    *out_ch = rx2[1]; *out_cfg = rx4[1];
    return (rx2[1] == 76u) && ((rx4[1] & PWR_UP) != 0);
}
static uint8_t probe_cs_mode(void){
    mss_spi_slave_t ss_opts[] = { MSS_SPI_SLAVE_0, MSS_SPI_SLAVE_1 };
    mss_spi_protocol_mode_t md_opts[] = { MSS_SPI_MODE0, MSS_SPI_MODE3 };
    char line[96];
    for (unsigned i=0;i<sizeof(ss_opts)/sizeof(ss_opts[0]);++i){
        for (unsigned j=0;j<sizeof(md_opts)/sizeof(md_opts[0]);++j){
            uint8_t ch=0, cfg=0;
            uint8_t ok = try_rw_ch_cfg(ss_opts[i], md_opts[j], &ch, &cfg);
            snprintf(line,sizeof line,"[TX] probe SS=%d MODE=%d -> CH=%u CFG=0x%02X\r\n",
                     (int)ss_opts[i], (int)md_opts[j], ch, cfg);
            LOG(line);
            if (ok){
                g_ss = ss_opts[i]; g_mode = md_opts[j];
                spi_cfg_for(g_ss, g_mode);
                snprintf(line,sizeof line,"[TX] Using SS=%d MODE=%d\r\n",(int)g_ss,(int)g_mode);
                LOG(line);
                return 1u;
            }
        }
    }
    return 0u;
}

/* ===== HDC2080 Register Defs (from I2C temp sensor.txt) ===== */
enum {
    REG_T_LOW      = 0x00,
    REG_T_HIGH     = 0x01,
    REG_RH_LOW     = 0x02,
    REG_RH_HIGH    = 0x03,
    REG_DRDY       = 0x04,
    REG_INT_EN     = 0x07,
    REG_RESET_CONF = 0x0E,
    REG_MEAS_CFG   = 0x0F,
    REG_MFR_ID_L   = 0xFC,
    REG_MFR_ID_H   = 0xFD,
    REG_DEV_ID_L   = 0xFE,
    REG_DEV_ID_H   = 0xFF
};

/* ===== I2C Helpers (from I2C temp sensor.txt) ===== */
static void i2c_sensor_completion_handler(mss_i2c_instance_t* i, mss_i2c_status_t s)
{ (void)i; (void)s; }

static mss_i2c_status_t i2c_write_u8(uint8_t dev7, uint8_t reg, uint8_t val)
{
    uint8_t frame[2] = { reg, val };
    MSS_I2C_write(I2C_SENSOR, dev7, frame, 2u, MSS_I2C_RELEASE_BUS);
    return MSS_I2C_wait_complete(I2C_SENSOR, DEMO_I2C_TIMEOUT);
}

static mss_i2c_status_t i2c_read_n(uint8_t dev7, uint8_t start_reg,
                                   uint8_t* buf, uint16_t len)
{
    MSS_I2C_write_read(I2C_SENSOR, dev7, &start_reg, 1u, buf, len, MSS_I2C_RELEASE_BUS);
    return MSS_I2C_wait_complete(I2C_SENSOR, DEMO_I2C_TIMEOUT);
}

static uint8_t i2c_probe(uint8_t addr7)
{
    uint8_t reg = REG_MFR_ID_L, rx = 0x00;
    MSS_I2C_write_read(I2C_SENSOR, addr7, &reg, 1u, &rx, 1u, MSS_I2C_RELEASE_BUS);
    return (MSS_I2C_wait_complete(I2C_SENSOR, 50u) == MSS_I2C_SUCCESS);
}

/* ===== HDC2080 Ops (from I2C temp sensor.txt) ===== */
static uint8_t hdc2080_soft_reset(void)
{
    if (MSS_I2C_SUCCESS != i2c_write_u8(g_dev_addr, REG_RESET_CONF, 0x80u))
        return 0u;
    delay_ms(10);
    return 1u;
}

static uint8_t hdc2080_init(void)
{
    uint8_t id[4] = {0};
    if (MSS_I2C_SUCCESS != i2c_read_n(g_dev_addr, REG_MFR_ID_L, id, 4u))
        return 0u;
    uint16_t mfr = ((uint16_t)id[1] << 8) | id[0];
    uint16_t dev = ((uint16_t)id[3] << 8) | id[2];
    if (mfr != 0x5449u || dev != 0x07D0u)
        return 0u;

    if (MSS_I2C_SUCCESS != i2c_write_u8(g_dev_addr, REG_MEAS_CFG, 0x00u)) return 0u;
    if (MSS_I2C_SUCCESS != i2c_write_u8(g_dev_addr, REG_INT_EN,  0x00u)) return 0u;
    return 1u;
}

static void hdc2080_trigger(void)
{
    (void)i2c_write_u8(g_dev_addr, REG_MEAS_CFG, 0x01u);
}

static uint8_t hdc2080_drdy(void)
{
    uint8_t v = 0;
    if (MSS_I2C_SUCCESS != i2c_read_n(g_dev_addr, REG_DRDY, &v, 1u))
        return 0u;
    return (uint8_t)((v & 0x80u) ? 1u : 0u);
}

static uint8_t hdc2080_read_trh_centi(int32_t* t_centi, int32_t* rh_centi)
{
    uint8_t rx[4];
    if (MSS_I2C_SUCCESS != i2c_read_n(g_dev_addr, REG_T_LOW, rx, 4u))
        return 0u;

    uint16_t t_raw  = ((uint16_t)rx[1] << 8) | rx[0];
    uint16_t rh_raw = ((uint16_t)rx[3] << 8) | rx[2];

    *t_centi  = ((int32_t)t_raw  * 16500) / 65536 - 4000;
    *rh_centi = ((int32_t)rh_raw * 10000) / 65536;

    return 1u;
}

/* ===== UART Print Helper (from I2C temp sensor.txt) ===== */
static void uart_print_centi(const char* label, int32_t centi, const char* unit)
{
    char buf[48];
    int32_t abs_c = (centi < 0) ? -centi : centi;
    (void)snprintf(buf, sizeof buf, "%s %s%d.%02d %s",
                   label, (centi < 0 ? "-" : ""), (int)(abs_c / 100), (int)(abs_c % 100), unit);
    LOG(buf);
}

/* ===== SysTick (from I2C temp sensor.txt) ===== */
void SysTick_Handler_h1_IRQHandler(void)
{
    MSS_I2C_system_tick(I2C_SENSOR, 10);  /* 10 ms tick */
}

/* ===== Hart entry (U54_1) - MERGED ===== */
void u54_1(void)
{
    wait_for_boot();

    /* Clocks + fabric */
    (void)mss_config_clk_rst(MSS_PERIPH_MMUART_U54_1, (uint8_t)1, PERIPHERAL_ON);
    (void)mss_config_clk_rst(SENSOR_PERIPH,           (uint8_t)1, PERIPHERAL_ON); /* I2C0 */
    (void)mss_config_clk_rst(MSS_PERIPH_SPI0,         (uint8_t)1, PERIPHERAL_ON); /* SPI0 */
    (void)mss_config_clk_rst(MSS_PERIPH_GPIO1,        (uint8_t)1, PERIPHERAL_ON); /* GPIO1 */
    mss_enable_fabric();

    /* IRQs (from I2C temp sensor.txt) */
    PLIC_init();
    PLIC_SetPriority(I2C0_MAIN_PLIC, 2);
    __enable_irq();

    /* SysTick (from I2C temp sensor.txt) */
    (void)SysTick_Config();

    /* UART Init (from I2C temp sensor.txt) */
    g_uart = p_uartmap_u54_1;
    MSS_UART_init(g_uart, MSS_UART_115200_BAUD,
                  MSS_UART_DATA_8_BITS | MSS_UART_NO_PARITY | MSS_UART_ONE_STOP_BIT);
    LOG("\r\n[TX NODE] BOOT\r\n");

    /* GPIO Init (for nRF CE) - From tx1-10s.txt */
    MSS_GPIO_init(NRF_GPIO_PORT);
    MSS_GPIO_config(NRF_GPIO_PORT, NRF_CE_PIN,  MSS_GPIO_OUTPUT_MODE);
    MSS_GPIO_config(NRF_GPIO_PORT, NRF_IRQ_PIN, MSS_GPIO_INPUT_MODE);
    ce_low();

    /* SPI0 Init (for nRF) - From tx1-10s.txt */
    MSS_SPI_init(NRF_SPIx);
    if (!probe_cs_mode()){
        LOG("[TX] nRF ERROR: No SS/MODE matched.\r\n");
        for(;;){}
    }

    /* I2C0 Init (for Sensor) - From I2C temp sensor.txt */
    MSS_I2C_init(I2C_SENSOR, MASTER_SER_ADDR, I2C_CLKDIV);
    MSS_I2C_register_transfer_completion_handler(I2C_SENSOR, i2c_sensor_completion_handler);
    delay_ms(3);

    /* Autodetect HDC2080 (from I2C temp sensor.txt) */
    if (i2c_probe(0x40u))      g_dev_addr = 0x40u;
    else if (i2c_probe(0x41u)) g_dev_addr = 0x41u;
    else {
        LOG("[TX] HDC2080 ERROR: no ACK at 0x40/0x41\r\n");
        for(;;){ delay_ms(500); }
    }

    /* Init HDC2080 (from I2C temp sensor.txt) */
    if (!hdc2080_soft_reset() || !hdc2080_init()) {
        LOG("[TX] HDC2080 ERROR: init/ID failed\r\n");
        for(;;){ delay_ms(500); }
    }
    LOG("[TX] HDC2080 Initialized.\r\n");

    /* ===== CW beacon (from tx1-10s.txt) ===== */
#if DO_CW_TEST
    cw_test();
#endif

    /* ===== nRF Packet setup (from tx1-10s.txt) ===== */
    nrf_set_channel(76);
    nrf_flush_rx(); nrf_flush_tx(); nrf_clear_irqs();
    nrf_set_rf_1M_0dBm();

    nrf_write_reg(REG_SETUP_AW, 0x03);
    const uint8_t addr[ADDR_LEN] = {0xE7,0xE7,0xE7,0xE7,0xE7};
    nrf_write_buf(REG_TX_ADDR,   addr, ADDR_LEN);
    nrf_write_buf(REG_RX_ADDR_P0,addr, ADDR_LEN);
    nrf_write_reg(REG_EN_AA,     0x00);
    nrf_write_reg(REG_EN_RXADDR, 0x01);
    nrf_write_reg(REG_SETUP_RETR,0x00);
    nrf_write_reg(REG_RX_PW_P0,  PAYLOAD_LEN);

    uint8_t cfg = EN_CRC | PWR_UP;
    nrf_write_reg(REG_CONFIG, cfg);
    delay_us(1500);

    nrf_dump_regs();
    LOG("[TX] nRF Minimal TX ready. Sending 8B T/RH every 1s...\r\n");

    /* TX loop */
    uint8_t payload[PAYLOAD_LEN];
    char line[96];

    for (;;) {
        /* 1. Trigger (from I2C temp sensor.txt) */
        hdc2080_trigger();

        /* 2. Poll DRDY (from I2C temp sensor.txt) */
        uint8_t ready = 0u;
        for (int i = 0; i < 10; ++i) {
            if (hdc2080_drdy()) { ready = 1u; break; }
            delay_ms(1);
        }
        if (!ready) {
            LOG("[TX] HDC2080 DRDY timeout; re-init\r\n");
            (void)hdc2080_soft_reset();
            (void)hdc2080_init();
            continue;
        }

        /* 3. Read T/RH data (from I2C temp sensor.txt) */
        int32_t t_centi = 0, rh_centi = 0;
        if (!hdc2080_read_trh_centi(&t_centi, &rh_centi)) {
            LOG("[TX] HDC2080 read error\r\n");
            continue;
        }

        /* 4. Log to UART (from I2C temp sensor.txt) */
        LOG("T = ");
        uart_print_centi("", t_centi, "C");
        LOG(", RH = ");
        uart_print_centi("", rh_centi, "%");
        LOG(" -> ");

        /* 5. Prepare 8-byte payload (Integration step) */
        memcpy(payload, &t_centi, sizeof(int32_t));
        memcpy(payload + sizeof(int32_t), &rh_centi, sizeof(int32_t));

        /* 6. Transmit payload (from tx1-10s.txt) */
        nrf_send_payload(payload, PAYLOAD_LEN);
        uint8_t st = nrf_read_reg(REG_STATUS);

        snprintf(line, sizeof line, "SENT  len=%u  STATUS=0x%02X\r\n",
                 (unsigned)PAYLOAD_LEN, st);
        LOG(line);

        /* 7. Wait 1 second (from I2C temp sensor.txt) */
        delay_ms(1000);
    }
}

/* ===== Optional SWI for hart1 (from I2C temp sensor.txt) ===== */
void Software_h1_IRQHandler(void) { /* no-op */ }

