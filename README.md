# [Demo Videosu](https://www.youtube.com/watch?v=h-fNTO8wqw0)
# STM32 NUCLEO-F446RE + EBYTE E220-400T22D LoRa Sürücüsü
---

## İçindekiler
- [Donanım ve Pin Haritası](#donanım-ve-pin-haritası)
- [CubeMX Ayar Özeti](#cubemx-ayar-özeti)
- [Proje Yapısı](#proje-yapısı)
- [Derleme ve Yükleme](#derleme-ve-yükleme)
- [Uygulama Modları (READ / WRITE)](#uygulama-modları-read--write)
- [Live Expressions ile İzleme](#live-expressions-ile-izleme)
- [Sürücü API'si](#sürücü-apisi)
- [Konfigürasyon Yapısı ve Enum'lar](#konfigürasyon-yapısı-ve-enumlar)
- [E220 Register Haritası](#e220-register-haritası)
- [Mod / AUX Mantığı](#mod--aux-mantığı)
- [Baud (UART Hızı) Mantığı — Önemli](#baud-uart-hızı-mantığı--önemli)
- [Sorun Giderme](#sorun-giderme)
---
## Donanım ve Pin Haritası
| İşlev | STM32 Pini | E220 Pini / Not |
|------|-----------|-----------------|
| USART1 TX | PA9  | E220 RXD (haberleşme, 9600 8N1) |
| USART1 RX | PA10 | E220 TXD |
| Mod seçimi M0 | PB0 | GPIO çıkış |
| Mod seçimi M1 | PB1 | GPIO çıkış |
| AUX (meşgul/hazır) | PA8 | GPIO giriş |
| Durum LED'i (LD2) | PA5 | Her gönderimde toggle |
> **Kablolama:** UART hatları çaprazdır — STM32 **TX (PA9) → E220 RXD**, STM32 **RX (PA10) → E220 TXD**.
> E220 **3.3 V** ile beslenir. GND ortak olmalıdır.
**Frekans:** `F(MHz) = 410.125 + channel`. Örn. `channel = 55` → **465.125 MHz**.
---
## CubeMX Ayar Özeti
`.ioc` dosyasını CubeMX ile açtığında (veya sıfırdan kurarken) etkinleştirilen ayarlar:
- **RCC:** HSI (16 MHz), PLL yok → `SYSCLK = HCLK = APB1 = APB2 = 16 MHz`.
- **SYS:** Debug = Serial Wire (SWD), Timebase = SysTick.
- **USART1** (Connectivity): Asynchronous, **9600** baud, 8 bit, Parity None, 1 stop → PA9/PA10 (AF7).
- **GPIO** (System Core → GPIO):
  - `PB0` = GPIO_Output (M0), `PB1` = GPIO_Output (M1) — Push-Pull, No pull, Low speed
  - `PA8` = GPIO_Input (AUX) — No pull
  - `PA5` = GPIO_Output (LD2) — Push-Pull, No pull, Low speed
> Not: USART2/printf debug hattı bilinçli olarak **kaldırılmıştır**; izleme Live Expressions ile yapılır.
---
## Proje Yapısı
```
lora_get_config_fw/
├── Core/
│   ├── Inc/
│   │   ├── e220.h          # Sürücü arayüzü (config struct, enum'lar, API)
│   │   ├── main.h
│   │   └── stm32f4xx_hal_conf.h
│   └── Src/
│       ├── e220.c          # Sürücü uygulaması (register pack/unpack, mod, R/W, TX/RX)
│       ├── main.c          # Uygulama akışı (READ/WRITE modu + veri döngüsü)
│       ├── stm32f4xx_hal_msp.c
│       └── ...
├── lora_get_config_fw.ioc  # CubeMX proje dosyası
└── README.md
```
---
## Derleme ve Yükleme
1. Projeyi **STM32CubeIDE** ile aç (`File → Open Projects from File System...`).
2. `Core/Src/main.c` içinde **uygulama modunu** seç (bkz. aşağıdaki bölüm).
3. `Project → Build Project` (Ctrl+B).
4. NUCLEO kartını USB ile bağla, `Run → Debug` (F11) ile ST-Link üzerinden yükle.
5. Debug oturumunda **Live Expressions** penceresini aç ve ilgili değişkenleri ekle.
> `Core/` ve `Drivers/` kaynak klasör olarak tanımlı; `e220.c` otomatik derlenir,
> `Core/Inc` include yolundadır — ekstra ayar gerekmez.
---
## Uygulama Modları (READ / WRITE)
### Neden Config Modu Hep 9600?

E220'de iki ayrı hat vardır: STM32 ↔ E220 arasındaki **UART kablosu** (`uart_baud`)
ve E220 ↔ karşı modül arasındaki **radyo** (`air_rate`). Bunlar birbirinden bağımsızdır.

Modülün önemli bir özelliği şudur: **Config modunda (Mod 3, M0=1 M1=1) E220'nin UART'ı,
ayarlanan `uart_baud` ne olursa olsun DAİMA 9600 8N1 çalışır.** Ayar okuma/yazma (0xC1/0xC0/0xC2)
komutları bu yüzden her zaman 9600'de gönderilir. Normal modda (Mod 0) ise modül, ayarlı
`uart_baud` hızını kullanır.

Bu nedenle telin iki ucu her an aynı hızda olmalıdır. Sürücü bunu **otomatik yönetir**:
mod değişiminde `huart1` baud'unu Config modunda 9600'e, Normal/WOR modunda ise ayarlı
`uart_baud` değerine getirir. Yani `uart_baud`'u 9600 dışında bir değere ayarlasan bile
elle bir şey yapman gerekmez; hem config hem veri iletişimi doğru çalışır.

> Özet: `uart_baud` sadece kablo hızıdır, menzille ilgisi yoktur. İki modülün
> haberleşmesi için eşleşmesi gereken ayarlar `channel` ve `air_rate`'tir.
`main.c` içinde, pin tanımlarının altında tek satırla seçilir:
```c
#define E220_APP_READ    0
#define E220_APP_WRITE   1
#define E220_APP_MODE    E220_APP_READ   // <-- burayı değiştir
```
### READ modu (`E220_APP_READ`)
- Açılışta modülü **sadece okur** (`E220_ReadConfig` → `g_e220_cfg`).
- Modüle **hiçbir şey yazmaz**, mevcut ayarı korur.
- İzle: **`g_e220_cfg`**, **`g_read_status`** (0 = `E220_OK`).
### WRITE modu (`E220_APP_WRITE`)
- `main.c`'deki "WRITE MODU" bloğundaki değerleri modüle **yazar**.
- **Geri okuma / doğrulama yapmaz.**
- İzle: **`g_write_status`** (0 = `E220_OK`).
- Yazma **kalıcıdır** (`permanent=true`, komut `0xC0` → flash). Geçici istersen `false` yap (`0xC2`).
**Önerilen kullanım:** Bir kez `E220_APP_WRITE` ile yükle → `g_write_status == 0` gör →
sonra `E220_APP_READ`'e dön ve tekrar yükle. Böylece her açılışta flash'a yazılmaz
(flash ömrü korunur) ve `g_e220_cfg` üzerinden ayarların oturduğu doğrulanır.
Her iki modda da ana döngü, ayrıca 2 sn'de bir `"Hello LoRa #N\r\n"` gönderir, gelen
veriyi dinler ve her gönderimde LD2'yi toggle eder.
---
## Live Expressions ile İzleme
Debug sırasında breakpoint koymadan gerçek zamanlı izlenebilen global değişkenler:
| Değişken | Anlamı |
|----------|--------|
| `g_e220_cfg` | Aktif konfigürasyon (READ'de okunan, WRITE'da yazılan). Alanları açmak için ▶ |
| `g_read_status` | (READ modu) `E220_ReadConfig` dönüş kodu; `0 = E220_OK` |
| `g_write_status` | (WRITE modu) `E220_WriteConfig` dönüş kodu; `0 = E220_OK` |
| `g_tx_count` | Gönderilen paket sayacı — 2 sn'de bir artar (heartbeat) |
| `g_last_send_status` | Son `E220_SendData` dönüş kodu |
| `g_last_rx_len` | Son alınan paketin bayt sayısı |
| `g_rx_buf` | Son alınan paketin içeriği (metin) |
Pin/register seviyesi (donanım doğrulaması için ifade olarak da eklenebilir):
| İfade | Anlamı |
|-------|--------|
| `(GPIOA->IDR >> 8) & 1` | AUX (PA8) anlık seviyesi (1 = hazır) |
| `GPIOB->ODR & 0x3` | M1:M0 (Config anında `3`, Normal `0`) |
| `(GPIOA->ODR >> 5) & 1` | LD2 (PA5) durumu |
> **İpucu:** Live Expressions'ta lokal değişkenlerin görünmesi için **Debug (-O0)**
> ile derleyin; global/register ifadeleri her durumda güvenlidir.
---
## Sürücü API'si
`e220.h` içinde tanımlı ana fonksiyonlar:
```c
E220_Status E220_Init(E220_Handle *h, UART_HandleTypeDef *huart,
                      GPIO_TypeDef *m0_port, uint16_t m0_pin,
                      GPIO_TypeDef *m1_port, uint16_t m1_pin,
                      GPIO_TypeDef *aux_port, uint16_t aux_pin);
E220_Status E220_SetMode(E220_Handle *h, E220_Mode mode);   // M0/M1 + AUX bekle
bool        E220_WaitAux(E220_Handle *h, uint32_t timeout_ms);
E220_Status E220_ReadConfig(E220_Handle *h, E220_Config *out);           // C1 00 08
E220_Status E220_WriteConfig(E220_Handle *h, const E220_Config *cfg, bool permanent); // C0 / C2
void        E220_PrintConfig(const E220_Config *cfg);        // printf (opsiyonel yardımcı)
E220_Status E220_SendData(E220_Handle *h, const uint8_t *buf, uint16_t len);
E220_Status E220_ReceiveData(E220_Handle *h, uint8_t *buf, uint16_t maxlen,
                             uint16_t *outlen, uint32_t timeout);
/* Yardımcı: ham register <-> config dönüşümü */
void E220_UnpackRegisters(const uint8_t regs[8], E220_Config *cfg);
void E220_PackRegisters(const E220_Config *cfg, uint8_t regs[8]);
