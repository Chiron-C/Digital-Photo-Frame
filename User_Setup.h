// ==========================================
// FISIER: User_Setup.h
// ==========================================

#define ST7789_DRIVER      
#define TFT_WIDTH  240
#define TFT_HEIGHT 320

// --- PINI ECRAN (VSPI) ---
#define TFT_MISO -1 
#define TFT_MOSI 23
#define TFT_SCLK 18
#define TFT_CS   15   
#define TFT_DC    2
#define TFT_RST   32  // <--- AICI ERA PROBLEMA (TREBUIE 32, NU 4)

// --- CONFIGURARE CULORI ---
#define TFT_RGB_ORDER TFT_BGR 
#define TFT_INVERSION_ON      

// --- FONTURI ---
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define SMOOTH_FONT

// --- VITEZA ---
#define SPI_FREQUENCY  30000000 

// Touch-ul il gestionam manual in cod, deci lasam comentat aici:
// #define TOUCH_CS ...