/**
  ******************************************************************************
  * @file    e220.c
  * @brief   EBYTE E220-400T22D LoRa modulu surucu katmani - uygulama.
  *
  *  Tum UART islemleri blocking HAL (HAL_UART_Transmit / HAL_UART_Receive) ile
  *  ve timeout kullanilarak yapilir. Register bit-manipulasyonlari maskelerle
  *  ve yorumlarla acikca belirtilmistir.
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "e220.h"
#include <stdio.h>

/* Private defines -----------------------------------------------------------*/
#define E220_UART_TX_TIMEOUT_MS   1000U  /* Tek komut/paket gonderim timeout'u */
#define E220_UART_RX_TIMEOUT_MS   1000U  /* Config cevabi bekleme timeout'u    */
#define E220_AUX_TIMEOUT_MS       1000U  /* AUX HIGH bekleme timeout'u         */
#define E220_AUX_SETTLE_MS        2U     /* AUX HIGH sonrasi ek yerlesme suresi*/
#define E220_MODE_SWITCH_DELAY_MS 5U     /* M0/M1 seviye yerlesmesi icin gecikme*/

#define E220_RX_INTERBYTE_MS      15U    /* Cerceve ici baytlar arasi bosluk   */

/* ===========================================================================
 *  Register bit maskeleri / kaydirmalari
 *
 *  REG0 (0x02): | b7 b6 b5 | b4 b3 |  b2 b1 b0 |
 *               |  UART    | PARITY|  AIR RATE |
 *  REG1 (0x03): | b7 b6 | b5  | b4 b3 b2 | b1 b0 |
 *               | SUBPKT| RSSI| (rezerv) | TXPWR |
 *  REG3 (0x05): | b7 | b6 | b5 | b4 | b3 | b2 b1 b0 |
 *               |RSSI|FIX |rez |LBT |rez |   WOR    |
 * =========================================================================== */
#define REG0_UART_Pos     5U
#define REG0_UART_Msk     (0x07U << REG0_UART_Pos)   /* 0b1110_0000 */
#define REG0_PARITY_Pos   3U
#define REG0_PARITY_Msk   (0x03U << REG0_PARITY_Pos) /* 0b0001_1000 */
#define REG0_AIR_Pos      0U
#define REG0_AIR_Msk      (0x07U << REG0_AIR_Pos)    /* 0b0000_0111 */

#define REG1_SUBPKT_Pos   6U
#define REG1_SUBPKT_Msk   (0x03U << REG1_SUBPKT_Pos) /* 0b1100_0000 */
#define REG1_RSSIAMB_Pos  5U
#define REG1_RSSIAMB_Msk  (0x01U << REG1_RSSIAMB_Pos)/* 0b0010_0000 */
#define REG1_TXPWR_Pos    0U
#define REG1_TXPWR_Msk    (0x03U << REG1_TXPWR_Pos)  /* 0b0000_0011 */

#define REG3_RSSIBYTE_Pos 7U
#define REG3_RSSIBYTE_Msk (0x01U << REG3_RSSIBYTE_Pos)/* 0b1000_0000 */
#define REG3_FIXED_Pos    6U
#define REG3_FIXED_Msk    (0x01U << REG3_FIXED_Pos)   /* 0b0100_0000 */
#define REG3_LBT_Pos      4U
#define REG3_LBT_Msk      (0x01U << REG3_LBT_Pos)     /* 0b0001_0000 */
#define REG3_WOR_Pos      0U
#define REG3_WOR_Msk      (0x07U << REG3_WOR_Pos)     /* 0b0000_0111 */

/* Yardimci: enum degerinin ilgili maskeye oturtulmus hali ------------------ */
#define FIELD_SET(val, pos, msk)  (((uint8_t)(val) << (pos)) & (msk))
#define FIELD_GET(reg, pos, msk)  (((uint8_t)(reg) & (msk)) >> (pos))

/* Config modunda modul UART'i DAIMA 9600 8N1 calisir (baud ayarindan bagimsiz)*/
#define E220_CONFIG_BAUD          9600U

/** @brief E220_UartBaud enum'unu gercek bps degerine cevirir. */
static uint32_t e220_baud_enum_to_val(E220_UartBaud b)
{
  static const uint32_t lut[8] =
      { 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200 };
  return lut[(uint8_t)b & 0x07U];
}

/**
  * @brief  huart baud hizini istenen degere getirir (gerekiyorsa yeniden init).
  *         Boylece config modu (9600) ile veri modu (data_baud) arasindaki
  *         hiz farki otomatik yonetilir.
  */
static E220_Status e220_apply_uart_baud(E220_Handle *h, uint32_t baud)
{
  if (h->huart->Init.BaudRate == baud) {
    return E220_OK;  /* zaten dogru hiz; gereksiz re-init yapma */
  }
  h->huart->Init.BaudRate = baud;
  if (HAL_UART_Init(h->huart) != HAL_OK) {
    return E220_ERR_UART;
  }
  return E220_OK;
}

/* ===========================================================================
 *  Yardimci - AUX bekleme
 * =========================================================================== */
bool E220_WaitAux(E220_Handle *h, uint32_t timeout_ms)
{
  if (h == NULL || h->aux_port == NULL) {
    return false;
  }

  uint32_t start = HAL_GetTick();

  /* AUX HIGH => modul bosta/hazir. LOW => modul mesgul. */
  while (HAL_GPIO_ReadPin(h->aux_port, h->aux_pin) == GPIO_PIN_RESET) {
    if ((HAL_GetTick() - start) >= timeout_ms) {
      return false;  /* zaman asimi: modul hazir hale gelmedi */
    }
  }

  /* HIGH olduktan sonra icsel islemlerin tamamlanmasi icin biraz daha bekle */
  HAL_Delay(E220_AUX_SETTLE_MS);
  return true;
}

/* ===========================================================================
 *  Mod ayari (M0/M1)
 * =========================================================================== */
E220_Status E220_SetMode(E220_Handle *h, E220_Mode mode)
{
  if (h == NULL || !h->initialized) {
    return E220_ERR_PARAM;
  }

  /* Mod degeri = (M1 << 1) | M0  =>  M0 = bit0, M1 = bit1 */
  GPIO_PinState m0 = (mode & 0x01U) ? GPIO_PIN_SET : GPIO_PIN_RESET;
  GPIO_PinState m1 = (mode & 0x02U) ? GPIO_PIN_SET : GPIO_PIN_RESET;

  HAL_GPIO_WritePin(h->m0_port, h->m0_pin, m0);
  HAL_GPIO_WritePin(h->m1_port, h->m1_pin, m1);

  /* Pin seviyelerinin yerlesmesi + modulun modu algilamasi icin kisa bekleme */
  HAL_Delay(E220_MODE_SWITCH_DELAY_MS);

  /* UART hizini moda gore ayarla: Config modu -> daima 9600, aksi halde
     ayarlanmis veri hizi (data_baud). */
  uint32_t target_baud = (mode == E220_MODE_CONFIG) ? E220_CONFIG_BAUD
                                                    : h->data_baud;
  E220_Status bst = e220_apply_uart_baud(h, target_baud);
  if (bst != E220_OK) {
    return bst;
  }

  /* Mod degisiminden sonra AUX tekrar HIGH olana kadar bekle */
  if (!E220_WaitAux(h, E220_AUX_TIMEOUT_MS)) {
    return E220_ERR_TIMEOUT;
  }

  return E220_OK;
}

/* ===========================================================================
 *  Baslatma
 * =========================================================================== */
E220_Status E220_Init(E220_Handle *h,
                      UART_HandleTypeDef *huart,
                      GPIO_TypeDef *m0_port, uint16_t m0_pin,
                      GPIO_TypeDef *m1_port, uint16_t m1_pin,
                      GPIO_TypeDef *aux_port, uint16_t aux_pin)
{
  if (h == NULL || huart == NULL ||
      m0_port == NULL || m1_port == NULL || aux_port == NULL) {
    return E220_ERR_PARAM;
  }

  h->huart    = huart;
  h->m0_port  = m0_port;  h->m0_pin  = m0_pin;
  h->m1_port  = m1_port;  h->m1_pin  = m1_pin;
  h->aux_port = aux_port; h->aux_pin = aux_pin;
  /* Modulun fabrika varsayilani veri baud'u 9600'dur; ilk ReadConfig'te
     modulun gercek ayarina gore guncellenecek. */
  h->data_baud = E220_CONFIG_BAUD;
  h->initialized = true;

  /* Baslangicta Normal moda al (veri gonderme/almaya hazir) */
  return E220_SetMode(h, E220_MODE_NORMAL);
}

/* ===========================================================================
 *  Register <-> Config donusumleri
 * =========================================================================== */
void E220_UnpackRegisters(const uint8_t regs[E220_CFG_LEN], E220_Config *cfg)
{
  /* 0x00 ADDH, 0x01 ADDL */
  cfg->addh = regs[E220_REG_ADDH];
  cfg->addl = regs[E220_REG_ADDL];

  /* 0x02 REG0 */
  cfg->uart_baud = (E220_UartBaud)FIELD_GET(regs[E220_REG_REG0], REG0_UART_Pos,   REG0_UART_Msk);
  cfg->parity    = (E220_Parity)  FIELD_GET(regs[E220_REG_REG0], REG0_PARITY_Pos, REG0_PARITY_Msk);
  cfg->air_rate  = (E220_AirRate) FIELD_GET(regs[E220_REG_REG0], REG0_AIR_Pos,    REG0_AIR_Msk);

  /* 0x03 REG1 */
  cfg->sub_packet   = (E220_SubPacket)FIELD_GET(regs[E220_REG_REG1], REG1_SUBPKT_Pos, REG1_SUBPKT_Msk);
  cfg->rssi_ambient = (bool)          FIELD_GET(regs[E220_REG_REG1], REG1_RSSIAMB_Pos, REG1_RSSIAMB_Msk);
  cfg->tx_power     = (E220_TxPower)  FIELD_GET(regs[E220_REG_REG1], REG1_TXPWR_Pos,  REG1_TXPWR_Msk);

  /* 0x04 REG2 : kanal */
  cfg->channel = regs[E220_REG_REG2];

  /* 0x05 REG3 */
  cfg->rssi_byte          = (bool)         FIELD_GET(regs[E220_REG_REG3], REG3_RSSIBYTE_Pos, REG3_RSSIBYTE_Msk);
  cfg->fixed_transmission = (bool)         FIELD_GET(regs[E220_REG_REG3], REG3_FIXED_Pos,    REG3_FIXED_Msk);
  cfg->lbt                = (bool)         FIELD_GET(regs[E220_REG_REG3], REG3_LBT_Pos,      REG3_LBT_Msk);
  cfg->wor_cycle          = (E220_WorCycle)FIELD_GET(regs[E220_REG_REG3], REG3_WOR_Pos,      REG3_WOR_Msk);
}

void E220_PackRegisters(const E220_Config *cfg, uint8_t regs[E220_CFG_LEN])
{
  /* 0x00 ADDH, 0x01 ADDL */
  regs[E220_REG_ADDH] = cfg->addh;
  regs[E220_REG_ADDL] = cfg->addl;

  /* 0x02 REG0 : UART | PARITY | AIR */
  regs[E220_REG_REG0] = FIELD_SET(cfg->uart_baud, REG0_UART_Pos,   REG0_UART_Msk)
                      | FIELD_SET(cfg->parity,    REG0_PARITY_Pos, REG0_PARITY_Msk)
                      | FIELD_SET(cfg->air_rate,  REG0_AIR_Pos,    REG0_AIR_Msk);

  /* 0x03 REG1 : SUBPKT | RSSI_AMB | (rezerv=0) | TXPWR */
  regs[E220_REG_REG1] = FIELD_SET(cfg->sub_packet,          REG1_SUBPKT_Pos,  REG1_SUBPKT_Msk)
                      | FIELD_SET(cfg->rssi_ambient ? 1U:0U, REG1_RSSIAMB_Pos, REG1_RSSIAMB_Msk)
                      | FIELD_SET(cfg->tx_power,            REG1_TXPWR_Pos,   REG1_TXPWR_Msk);

  /* 0x04 REG2 : kanal */
  regs[E220_REG_REG2] = cfg->channel;

  /* 0x05 REG3 : RSSI_BYTE | FIXED | (rezerv) | LBT | (rezerv) | WOR */
  regs[E220_REG_REG3] = FIELD_SET(cfg->rssi_byte ? 1U:0U,          REG3_RSSIBYTE_Pos, REG3_RSSIBYTE_Msk)
                      | FIELD_SET(cfg->fixed_transmission ? 1U:0U, REG3_FIXED_Pos,    REG3_FIXED_Msk)
                      | FIELD_SET(cfg->lbt ? 1U:0U,                REG3_LBT_Pos,      REG3_LBT_Msk)
                      | FIELD_SET(cfg->wor_cycle,                  REG3_WOR_Pos,      REG3_WOR_Msk);

  /* 0x06/0x07 CRYPT : yalnizca yazilir; okumada 0 doner. Burada 0 birakiyoruz.
     (Gizli anahtar guvenlik nedeniyle geri okunamaz.) */
  regs[E220_REG_CRYPT_H] = 0x00U;
  regs[E220_REG_CRYPT_L] = 0x00U;
}

/* ===========================================================================
 *  UART yardimcilari
 * =========================================================================== */

/** @brief RX FIFO/registerinda bekleyen artik baytlari temizler (best-effort).*/
static void e220_flush_rx(E220_Handle *h)
{
  uint8_t dummy;
  /* 0 ms timeout ile: bekleyen bayt yoksa hemen doner */
  while (HAL_UART_Receive(h->huart, &dummy, 1, 0) == HAL_OK) {
    /* atilan bayt */
  }
  __HAL_UART_CLEAR_OREFLAG(h->huart);  /* olasi overrun bayragini temizle */
}

/* ===========================================================================
 *  Konfigurasyon okuma
 * =========================================================================== */
E220_Status E220_ReadConfig(E220_Handle *h, E220_Config *out)
{
  if (h == NULL || !h->initialized || out == NULL) {
    return E220_ERR_PARAM;
  }

  E220_Status st;

  /* 1) Config moduna gec (M0=1, M1=1) */
  st = E220_SetMode(h, E220_MODE_CONFIG);
  if (st != E220_OK) {
    return st;
  }

  e220_flush_rx(h);

  /* 2) Oku komutu: 0xC1 + start_addr(0x00) + length(0x08) */
  uint8_t cmd[3] = { E220_CMD_READ, E220_REG_ADDH, E220_CFG_LEN };
  if (HAL_UART_Transmit(h->huart, cmd, sizeof(cmd), E220_UART_TX_TIMEOUT_MS) != HAL_OK) {
    E220_SetMode(h, E220_MODE_NORMAL);
    return E220_ERR_UART;
  }

  /* 3) Cevap: 0xC1 + addr + length + data[8] = 3 + 8 = 11 bayt */
  uint8_t resp[3 + E220_CFG_LEN];
  if (HAL_UART_Receive(h->huart, resp, sizeof(resp), E220_UART_RX_TIMEOUT_MS) != HAL_OK) {
    E220_SetMode(h, E220_MODE_NORMAL);
    return E220_ERR_TIMEOUT;
  }

  /* 4) Cevap basligini dogrula */
  if (resp[0] != E220_CMD_READ || resp[1] != E220_REG_ADDH || resp[2] != E220_CFG_LEN) {
    E220_SetMode(h, E220_MODE_NORMAL);
    return E220_ERR_BAD_RESPONSE;
  }

  /* 5) Register baytlarini enum alanlarina ayristir */
  E220_UnpackRegisters(&resp[3], out);

  /* Modulun veri modu UART hizini ogrendik; handle'i guncelle ki Normal moda
     donuste huart dogru hiza ayarlansin. */
  h->data_baud = e220_baud_enum_to_val(out->uart_baud);

  /* 6) Normal moda geri don */
  return E220_SetMode(h, E220_MODE_NORMAL);
}

/* ===========================================================================
 *  Konfigurasyon yazma
 * =========================================================================== */
E220_Status E220_WriteConfig(E220_Handle *h, const E220_Config *cfg, bool permanent)
{
  if (h == NULL || !h->initialized || cfg == NULL) {
    return E220_ERR_PARAM;
  }

  E220_Status st;

  /* Config yapisi -> 8 baytlik ham register dizisi */
  uint8_t regs[E220_CFG_LEN];
  E220_PackRegisters(cfg, regs);

  /* Yaz komutu: kalici(0xC0) veya gecici(0xC2) */
  uint8_t cmd = permanent ? E220_CMD_WRITE_PERM : E220_CMD_WRITE_TEMP;

  /* Gonderilecek cerceve: cmd + start_addr + length + data[8] */
  uint8_t frame[3 + E220_CFG_LEN];
  frame[0] = cmd;
  frame[1] = E220_REG_ADDH;
  frame[2] = E220_CFG_LEN;
  for (uint8_t i = 0; i < E220_CFG_LEN; i++) {
    frame[3 + i] = regs[i];
  }

  /* 1) Config moduna gec */
  st = E220_SetMode(h, E220_MODE_CONFIG);
  if (st != E220_OK) {
    return st;
  }

  e220_flush_rx(h);

  /* 2) Cerceveyi gonder */
  if (HAL_UART_Transmit(h->huart, frame, sizeof(frame), E220_UART_TX_TIMEOUT_MS) != HAL_OK) {
    E220_SetMode(h, E220_MODE_NORMAL);
    return E220_ERR_UART;
  }

  /* 3) Cevap: modul yazilan registerlari 0xC1 basligiyla echo'lar
     (bazi urun yazilimlari komut baytini aynen echo'lar; ikisini de kabul et) */
  uint8_t resp[3 + E220_CFG_LEN];
  if (HAL_UART_Receive(h->huart, resp, sizeof(resp), E220_UART_RX_TIMEOUT_MS) != HAL_OK) {
    E220_SetMode(h, E220_MODE_NORMAL);
    return E220_ERR_TIMEOUT;
  }

  if ((resp[0] != E220_CMD_READ && resp[0] != cmd) ||
      resp[1] != E220_REG_ADDH || resp[2] != E220_CFG_LEN) {
    E220_SetMode(h, E220_MODE_NORMAL);
    return E220_ERR_BAD_RESPONSE;
  }

  /* Yazilan verinin geri okunan ile ayni olup olmadigini dogrula
     (CRYPT baytlari 0 okunacagi icin ilk 6 register'i karsilastir) */
  for (uint8_t i = 0; i < E220_REG_CRYPT_H; i++) {
    if (resp[3 + i] != regs[i]) {
      E220_SetMode(h, E220_MODE_NORMAL);
      return E220_ERR_BAD_RESPONSE;
    }
  }

  /* Yeni veri modu UART hizini handle'a yansit ki Normal moda donuste huart
     dogru hiza ayarlansin (config modu daima 9600 idi). */
  h->data_baud = e220_baud_enum_to_val(cfg->uart_baud);

  /* 4) Normal moda geri don */
  return E220_SetMode(h, E220_MODE_NORMAL);
}

/* ===========================================================================
 *  Insan-okur formatta yazdirma (printf -> USART2)
 * =========================================================================== */
void E220_PrintConfig(const E220_Config *cfg)
{
  static const uint32_t uart_baud_lut[8] =
      { 1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200 };
  static const char *air_rate_str[8] =
      { "2.4k", "2.4k", "2.4k", "4.8k", "9.6k", "19.2k", "38.4k", "62.5k" };
  static const char *parity_str[4] =
      { "8N1", "8O1", "8E1", "8N1" };
  static const uint16_t subpkt_lut[4] = { 200, 128, 64, 32 };
  static const uint8_t  txpwr_lut[4]  = { 22, 17, 13, 10 };

  if (cfg == NULL) {
    printf("E220_Config: (null)\r\n");
    return;
  }

  float freq = E220_BASE_FREQ_MHZ + (float)cfg->channel;

  printf("======== E220 Konfigurasyon ========\r\n");
  printf("  Adres (ADDH:ADDL) : 0x%02X:0x%02X (0x%04X)\r\n",
         cfg->addh, cfg->addl, ((uint16_t)cfg->addh << 8) | cfg->addl);
  printf("  UART baud         : %lu bps\r\n",
         (unsigned long)uart_baud_lut[cfg->uart_baud & 0x07]);
  printf("  Parity            : %s\r\n", parity_str[cfg->parity & 0x03]);
  printf("  Air data rate     : %s bps\r\n", air_rate_str[cfg->air_rate & 0x07]);
  printf("  Sub-packet        : %u byte\r\n", subpkt_lut[cfg->sub_packet & 0x03]);
  printf("  RSSI ambient      : %s\r\n", cfg->rssi_ambient ? "ACIK" : "kapali");
  printf("  TX power          : %u dBm\r\n", txpwr_lut[cfg->tx_power & 0x03]);
  printf("  Kanal (CH)        : %u  => %.3f MHz\r\n", cfg->channel, freq);
  printf("  RSSI byte         : %s\r\n", cfg->rssi_byte ? "ACIK" : "kapali");
  printf("  Iletim modu       : %s\r\n",
         cfg->fixed_transmission ? "Sabit (fixed)" : "Transparan");
  printf("  LBT               : %s\r\n", cfg->lbt ? "ACIK" : "kapali");
  printf("  WOR cycle         : %u ms\r\n",
         (unsigned)(((cfg->wor_cycle & 0x07) + 1) * 500));
  printf("=====================================\r\n");
}

/* ===========================================================================
 *  Veri gonderme (Normal mod)
 * =========================================================================== */
E220_Status E220_SendData(E220_Handle *h, const uint8_t *buf, uint16_t len)
{
  if (h == NULL || !h->initialized || buf == NULL || len == 0) {
    return E220_ERR_PARAM;
  }

  /* Gonderim yalnizca Normal modda anlamlidir. */
  E220_Status st = E220_SetMode(h, E220_MODE_NORMAL);
  if (st != E220_OK) {
    return st;
  }

  if (HAL_UART_Transmit(h->huart, (uint8_t *)buf, len, E220_UART_TX_TIMEOUT_MS) != HAL_OK) {
    return E220_ERR_UART;
  }

  /* Havadan gonderim tamamlanana kadar AUX LOW kalir; HIGH olmasini bekle. */
  if (!E220_WaitAux(h, E220_AUX_TIMEOUT_MS)) {
    return E220_ERR_TIMEOUT;
  }

  return E220_OK;
}

/* ===========================================================================
 *  Veri alma (Normal mod) - basit cerceveleme
 * =========================================================================== */
E220_Status E220_ReceiveData(E220_Handle *h, uint8_t *buf, uint16_t maxlen,
                             uint16_t *outlen, uint32_t timeout)
{
  if (h == NULL || !h->initialized || buf == NULL || outlen == NULL || maxlen == 0) {
    return E220_ERR_PARAM;
  }

  uint16_t idx = 0;

  /* 1) Ilk bayti `timeout` ms icinde bekle */
  if (HAL_UART_Receive(h->huart, &buf[0], 1, timeout) != HAL_OK) {
    *outlen = 0;
    return E220_ERR_TIMEOUT;  /* verilen sure icinde hic veri gelmedi */
  }
  idx = 1;

  /* 2) Baytlar arasi bosluk (E220_RX_INTERBYTE_MS) asilana ya da tampon
        dolana kadar okumaya devam et. Boylece degisken uzunluklu cerceve
        toplanir. */
  while (idx < maxlen) {
    if (HAL_UART_Receive(h->huart, &buf[idx], 1, E220_RX_INTERBYTE_MS) == HAL_OK) {
      idx++;
    } else {
      break;  /* inter-byte bosluk asildi => cerceve bitti */
    }
  }

  *outlen = idx;
  return E220_OK;
}
