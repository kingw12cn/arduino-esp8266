// ===================== 驱动选择 =====================
#define ILI9225_DRIVER          // 选择 ILI9225 驱动芯片

// ===================== 屏幕尺寸 =====================
#define TFT_WIDTH  176
#define TFT_HEIGHT 220

// ===================== ESP8266 引脚定义 =====================
// 使用硬件 SPI，只需要定义 CS、DC、RST 引脚
#define TFT_CS   D8    // 片选引脚
#define TFT_DC   D2    // 数据/命令引脚（原 RS/DC）
#define TFT_RST  D1    // 复位引脚

// SPI 引脚使用 ESP8266 默认硬件 SPI 引脚，无需定义
// MOSI = D7, SCLK = D5
// 如果需要软件 SPI，取消下方注释并注释掉上面的 TFT_CS/TFT_DC/TFT_RST
// #define TFT_MOSI D7
// #define TFT_SCLK D5
// #define TFT_CS   D8
// #define TFT_DC   D2
// #define TFT_RST  D1

// ===================== SPI 频率 =====================
#define SPI_FREQUENCY  27000000  // 27MHz
#define SPI_READ_FREQUENCY  20000000

// ===================== 字体配置 =====================
#define LOAD_GLCD      // 加载 8px 基础字体
#define LOAD_FONT2     // 加载 16px 字体
#define LOAD_FONT4     // 加载 26px 字体
#define LOAD_FONT6     // 加载 48px 字体
#define LOAD_FONT8     // 加载 75px 字体
#define LOAD_GFXFF     // 加载 GFX 字体
#define SMOOTH_FONT    // 启用平滑字体

// ===================== 可选：颜色顺序 =====================
// 如果显示颜色异常（如红蓝颠倒），取消下方注释
// #define TFT_RGB_ORDER TFT_BGR
