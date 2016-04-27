#ifndef _OLED_H
#define _OLED_H

typedef struct{
    float g;
    float l;
} solargraph_t;

extern "C" {

void displayOn(void);
void reset_display(void);
void displayOff(void);
void reset_display(void);
void clear_display();
void clear_display_row(unsigned char k);
void SendChar(unsigned char data);
void sendCharXY(unsigned char data, int X, int Y);
void sendcommand(unsigned char com);
void setXY(unsigned char row,unsigned char col);
void clear_display(void);
void sendStr(unsigned char *string);
void sendStrXY( char *string, int X, int Y);
void init_OLED(void);
void Draw_WIFI();
void Draw_WAVES();
void Draw_Plot(solargraph_t *slog, int fi, int maxn);
void StartUp_OLED();

}

#endif
