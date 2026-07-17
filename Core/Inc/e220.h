/**
  ******************************************************************************
  * @file    e220.h
  * @brief   EBYTE E220-400T22D (LLCC68 tabanli, UART arayuzlu) LoRa modulu
  *          surucu katmani - genel arayuz.
  *
  *  Hedef donanim : STM32 NUCLEO-F446RE (STM32F446RET6) + HAL kutuphanesi
  *
  *  Pin haritasi (bkz. main.c CubeMX ozeti):
  *    - USART1 (PA9=TX, PA10=RX) : E220 haberlesme UART'i, 9600 8N1
  *    - M0 = PB0, M1 = PB1       : mod secim pinleri (GPIO cikis)
  *    - AUX = PA8                : modul mesgul/hazir pini (GPIO giris)
  *
  *  E220, "config" modunda (M0=1, M1=1) daima 9600 8N1 calisir; komutlar bu
  *  moddan gonderilir. Veri gonderme/alma ise "normal" modda (M0=0, M1=0)
  *  yapilir.
  ******************************************************************************
  */

#ifndef __E220_H
#define __E220_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/* ============================================================================
 *  Register adresleri (0xC1/0xC0/0xC2 komutlarinda start_addr olarak kullanilir)
 * ==========================================================================*/
#define E220_REG_ADDH     0x00U  /* Adres yuksek bayt                          */
#define E220_REG_ADDL     0x01U  /* Adres dusuk bayt                           */
#define E220_REG_REG0     0x02U  /* [7:5] UART baud, [4:3] parity, [2:0] air   */
#define E220_REG_REG1     0x03U  /* [7:6] sub-packet, [5] RSSI ambient, [1:0]TX*/
#define E220_REG_REG2     0x04U  /* [7:0] kanal (channel)                      */
#define E220_REG_REG3     0x05U  /* [7] RSSI byte, [6] fixed, [4] LBT, [2:0]WOR */
#define E220_REG_CRYPT_H  0x06U  /* Sifreleme anahtari yuksek bayt (yalniz yaz)*/
#define E220_REG_CRYPT_L  0x07U  /* Sifreleme anahtari dusuk bayt  (yalniz yaz)*/

#define E220_CFG_LEN      0x08U  /* 0x00..0x07 => toplam 8 register            */

/* E220 komut baytlari -------------------------------------------------------*/
#define E220_CMD_READ         0xC1U  /* Oku : C1 + addr + len                  */
#define E220_CMD_WRITE_PERM   0xC0U  /* Yaz (kalici, flash'a)                  */
#define E220_CMD_WRITE_TEMP   0xC2U  /* Yaz (gecici, RAM - reset'te silinir)   */

/* Kanal <-> frekans donusumu: F(MHz) = 410.125 + channel                     */
#define E220_BASE_FREQ_MHZ    (410.125f)

/* ============================================================================
 *  Enum tanimlari - register bit alanlarinin insan-okur karsiliklari
 * ==========================================================================*/

/** @brief REG0[7:5] UART haberlesme hizi (config modunda degil, VERI modunda). */
typedef enum {
  E220_UART_1200   = 0x00,
  E220_UART_2400   = 0x01,
  E220_UART_4800   = 0x02,
  E220_UART_9600   = 0x03,  /* varsayilan */
  E220_UART_19200  = 0x04,
  E220_UART_38400  = 0x05,
  E220_UART_57600  = 0x06,
  E220_UART_115200 = 0x07
} E220_UartBaud;

/** @brief REG0[4:3] UART parity biti. */
typedef enum {
  E220_PARITY_8N1 = 0x00,  /* varsayilan */
  E220_PARITY_8O1 = 0x01,
  E220_PARITY_8E1 = 0x02,
  E220_PARITY_8N1_ALT = 0x03  /* 11 de 8N1'e karsilik gelir */
} E220_Parity;

/** @brief REG0[2:0] havadaki veri hizi (air data rate). */
typedef enum {
  E220_AIR_2K4  = 0x02,  /* 2.4 kbps (varsayilan) - 000/001 de 2.4k'dir */
  E220_AIR_4K8  = 0x03,  /* 4.8 kbps  */
  E220_AIR_9K6  = 0x04,  /* 9.6 kbps  */
  E220_AIR_19K2 = 0x05,  /* 19.2 kbps */
  E220_AIR_38K4 = 0x06,  /* 38.4 kbps */
  E220_AIR_62K5 = 0x07   /* 62.5 kbps */
} E220_AirRate;

/** @brief REG1[7:6] alt-paket (sub-packet) uzunlugu. */
typedef enum {
  E220_SUBPKT_200 = 0x00,  /* 200 byte (varsayilan) */
  E220_SUBPKT_128 = 0x01,  /* 128 byte */
  E220_SUBPKT_64  = 0x02,  /* 64 byte  */
  E220_SUBPKT_32  = 0x03   /* 32 byte  */
} E220_SubPacket;

/** @brief REG1[1:0] gonderim gucu. */
typedef enum {
  E220_TXP_22dBm = 0x00,  /* 22 dBm (varsayilan, en yuksek) */
  E220_TXP_17dBm = 0x01,
  E220_TXP_13dBm = 0x02,
  E220_TXP_10dBm = 0x03
} E220_TxPower;

/** @brief REG3[2:0] WOR (Wake-On-Radio) periyodu. Periyot = (deger+1)*500 ms. */
typedef enum {
  E220_WOR_500MS  = 0x00,
  E220_WOR_1000MS = 0x01,
  E220_WOR_1500MS = 0x02,
  E220_WOR_2000MS = 0x03,  /* varsayilan */
  E220_WOR_2500MS = 0x04,
  E220_WOR_3000MS = 0x05,
  E220_WOR_3500MS = 0x06,
  E220_WOR_4000MS = 0x07
} E220_WorCycle;

/** @brief Calisma modu. Deger = (M1 << 1) | M0. */
typedef enum {
  E220_MODE_NORMAL = 0x00,  /* M1=0 M0=0 : normal veri gonderme/alma          */
  E220_MODE_WOR_TX = 0x01,  /* M1=0 M0=1 : WOR verici                          */
  E220_MODE_WOR_RX = 0x02,  /* M1=1 M0=0 : WOR alici                           */
  E220_MODE_CONFIG = 0x03   /* M1=1 M0=1 : konfigurasyon / uyku (get/set burada)*/
} E220_Mode;

/** @brief Fonksiyon donus kodlari. */
typedef enum {
  E220_OK = 0,          /* Basarili                                           */
  E220_ERR_PARAM,       /* Gecersiz parametre / handle init edilmemis         */
  E220_ERR_TIMEOUT,     /* UART veya AUX zaman asimi                          */
  E220_ERR_BAD_RESPONSE,/* Modulden beklenmeyen/eksik cevap                   */
  E220_ERR_UART         /* HAL UART hata dondurdu                             */
} E220_Status;

/* ============================================================================
 *  Konfigurasyon yapisi - 8 register'in enum'lastirilmis hali
 * ==========================================================================*/
typedef struct {
  uint8_t         addh;              /* REG 0x00 : adres yuksek bayt           */
  uint8_t         addl;              /* REG 0x01 : adres dusuk bayt            */
  E220_UartBaud   uart_baud;         /* REG0[7:5]                              */
  E220_Parity     parity;            /* REG0[4:3]                              */
  E220_AirRate    air_rate;          /* REG0[2:0]                              */
  E220_SubPacket  sub_packet;        /* REG1[7:6]                              */
  bool            rssi_ambient;      /* REG1[5]   : ortam RSSI etkin mi        */
  E220_TxPower    tx_power;          /* REG1[1:0]                              */
  uint8_t         channel;           /* REG 0x04  : kanal (0..83)              */
  bool            rssi_byte;         /* REG3[7]   : alinan pakete RSSI baytı ekle*/
  bool            fixed_transmission;/* REG3[6]   : 1=sabit(fixed) 0=transparan */
  bool            lbt;               /* REG3[4]   : dinle-once-gonder (LBT)     */
  E220_WorCycle   wor_cycle;         /* REG3[2:0]                              */
} E220_Config;

/* ============================================================================
 *  Surucu tutamaci (handle)
 * ==========================================================================*/
typedef struct {
  UART_HandleTypeDef *huart;     /* E220 ile konusan UART (ornek: &huart1)     */
  GPIO_TypeDef       *m0_port;
  uint16_t            m0_pin;
  GPIO_TypeDef       *m1_port;
  uint16_t            m1_pin;
  GPIO_TypeDef       *aux_port;
  uint16_t            aux_pin;
  /* Veri (Normal/WOR) modundaki UART hizi (bps). Config modunda modul DAIMA
     9600 kullanir; bu yuzden mod degisiminde huart baud'u otomatik ayarlanir.
     ReadConfig/WriteConfig sonrasi modulun gercek ayarina gore guncellenir. */
  uint32_t            data_baud;
  bool                initialized;
} E220_Handle;

/* ============================================================================
 *  Genel API
 * ==========================================================================*/

/**
  * @brief  Surucuyu baslatir; UART ve GPIO tutamaclarini kaydeder, modulu
  *         Normal moda (Mod 0) alir.
  * @param  h        Surucu tutamaci (kullanici tarafindan tahsis edilir)
  * @param  huart    E220 UART tutamaci (USART1)
  * @param  m0_port  M0 pini GPIO portu     @param m0_pin  M0 pin maskesi
  * @param  m1_port  M1 pini GPIO portu     @param m1_pin  M1 pin maskesi
  * @param  aux_port AUX pini GPIO portu    @param aux_pin AUX pin maskesi
  * @retval E220_Status
  */
E220_Status E220_Init(E220_Handle *h,
                      UART_HandleTypeDef *huart,
                      GPIO_TypeDef *m0_port, uint16_t m0_pin,
                      GPIO_TypeDef *m1_port, uint16_t m1_pin,
                      GPIO_TypeDef *aux_port, uint16_t aux_pin);

/**
  * @brief  M0/M1 pinlerini ayarlayarak modun degistirir ve AUX'un tekrar
  *         HIGH olmasini bekler (+2 ms yerlesme).
  */
E220_Status E220_SetMode(E220_Handle *h, E220_Mode mode);

/**
  * @brief  AUX pini HIGH (bosta) olana kadar poll eder.
  * @retval true  -> AUX zamaninda HIGH oldu
  *         false -> zaman asimi
  */
bool E220_WaitAux(E220_Handle *h, uint32_t timeout_ms);

/**
  * @brief  Mod 3'e gecer, "0xC1 0x00 0x08" gonderir, 11 baytlik cevabi parse
  *         eder ve modulu Normal moda geri alir.
  */
E220_Status E220_ReadConfig(E220_Handle *h, E220_Config *out);

/**
  * @brief  Konfigurasyonu yazar. permanent=true ise 0xC0 (flash/kalici),
  *         false ise 0xC2 (RAM/gecici) komutu kullanilir.
  */
E220_Status E220_WriteConfig(E220_Handle *h, const E220_Config *cfg, bool permanent);

/**
  * @brief  Konfigurasyonu printf ile insan-okur formatta (USART2) yazdirir.
  */
void E220_PrintConfig(const E220_Config *cfg);

/**
  * @brief  Normal modda (Mod 0) veri gonderir.
  */
E220_Status E220_SendData(E220_Handle *h, const uint8_t *buf, uint16_t len);

/**
  * @brief  Normal modda veri alir. Ilk bayti `timeout` ms icinde bekler,
  *         sonra baytlar arasi kisa bosluk asilinca cerceveyi tamamlar.
  * @param  outlen  Alinan bayt sayisi (cikis)
  */
E220_Status E220_ReceiveData(E220_Handle *h, uint8_t *buf, uint16_t maxlen,
                             uint16_t *outlen, uint32_t timeout);

/* --- Yardimci (bit paketleme/ayristirma) fonksiyonlari --------------------*/

/**
  * @brief  8 baytlik ham register dizisini (0x00..0x07) E220_Config'e ayristirir.
  */
void E220_UnpackRegisters(const uint8_t regs[E220_CFG_LEN], E220_Config *cfg);

/**
  * @brief  E220_Config'i 8 baytlik ham register dizisine (0x00..0x07) paketler.
  */
void E220_PackRegisters(const E220_Config *cfg, uint8_t regs[E220_CFG_LEN]);

#ifdef __cplusplus
}
#endif

#endif /* __E220_H */
