#define F_CPU 8000000UL

#include <avr/io.h>
#include <util/delay.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#define set_bit(reg,pos) reg|=(1<<pos)
#define clr_bit(reg,pos) reg&=~(1<<pos)
#define tog_bit(reg,pos) reg^=(1<<pos)

/* =================================================
 * KEYPAD PROTEUS — SMALLCALC layout:
 *   7  8  9  /       index:  0  1  2  3
 *   4  5  6  x               4  5  6  7
 *   1  2  3  -               8  9  10 11
 *  C/R 0  =  +              12 13 14 15
 *
 * Col0-3 → PA0-PA3 (OUTPUT)
 * Row0-3 → PA4-PA7 (INPUT)
 * ================================================= */

/* ================= LCD ================= */
void port(char data){
    if(data&1) set_bit(PORTB,0); else clr_bit(PORTB,0);
    if(data&2) set_bit(PORTB,1); else clr_bit(PORTB,1);
    if(data&4) set_bit(PORTB,2); else clr_bit(PORTB,2);
    if(data&8) set_bit(PORTB,3); else clr_bit(PORTB,3);
}

void mode_select(char m){
    if(m==0) clr_bit(PORTB,5);
    else     set_bit(PORTB,5);
}

void enb_tri(){
    set_bit(PORTB,4);
    _delay_us(50);
    clr_bit(PORTB,4);
    _delay_us(200);
}

void lcd_send(char data, char mode){
    mode_select(mode);
    port(data >> 4);
    enb_tri();
    port(data & 0x0F);
    enb_tri();
}

void lcd_cmd(char cmd)   { lcd_send(cmd,  0); }
void lcd_data(char data) { lcd_send(data, 1); }

void lcd_init(){
    DDRB |= 0x3F;
    _delay_ms(20);
    lcd_cmd(0x02);
    lcd_cmd(0x28);
    lcd_cmd(0x0C);
    lcd_cmd(0x06);
    lcd_cmd(0x01);
}

void lcd_clear(){
    lcd_cmd(0x01);
    _delay_ms(2);
}

void lcd_gotoxy(int x, int y){
    int addr = (y==0) ? 0x00 : 0x40;
    lcd_cmd(0x80 + addr + x);
}

void lcd_puts(char *s){
    while(*s) lcd_data(*s++);
}

/* ================= KEYPAD ================= */
void key_init(){
    DDRA  = 0x0F;   /* PA0-PA3: OUTPUT (col), PA4-PA7: INPUT (row) */
    PORTA = 0xFF;   /* pullup tất cả */
}

unsigned char key_scan(){
    unsigned char cnt, key;

    for(cnt=0; cnt<4; cnt++){
        PORTA = 0xFF;
        PORTA &= ~(1<<cnt);         /* kéo thấp từng cột */
        key = PINA & 0xF0;          /* đọc 4 hàng (PA4-PA7) */

        if(key != 0xF0){
            while((PINA & 0xF0) != 0xF0); /* chờ nhả phím */
            break;
        }
    }

    if(cnt == 4) return 0xFF;       /* không có phím */

    /* Proteus KEYPAD-SMALLCALC — quét ngang theo hàng:
     *   cnt=0 (Col0): 7,4,1,CLR  → key=0,4,8,12
     *   cnt=1 (Col1): 8,5,2,0    → key=1,5,9,13
     *   cnt=2 (Col2): 9,6,3,=    → key=2,6,10,14
     *   cnt=3 (Col3): /,x,-,+    → key=3,7,11,15
     */
    switch(cnt){
        case 0:
            if(key==0xE0) return 0;   /* 7  */
            if(key==0xD0) return 4;   /* 4  */
            if(key==0xB0) return 8;   /* 1  */
            if(key==0x70) return 12;  /* CLR/ON/C */
            break;
        case 1:
            if(key==0xE0) return 1;   /* 8  */
            if(key==0xD0) return 5;   /* 5  */
            if(key==0xB0) return 9;   /* 2  */
            if(key==0x70) return 13;  /* 0  */
            break;
        case 2:
            if(key==0xE0) return 2;   /* 9  */
            if(key==0xD0) return 6;   /* 6  */
            if(key==0xB0) return 10;  /* 3  */
            if(key==0x70) return 14;  /* =  */
            break;
        case 3:
            if(key==0xE0) return 3;   /* /  (NEXT trong PT2) */
            if(key==0xD0) return 7;   /* x  */
            if(key==0xB0) return 11;  /* -  */
            if(key==0x70) return 15;  /* +  */
            break;
    }
    return 0xFF;
}

/* ================= BIẾN TOÀN CỤC ================= */
#define dp 3

char  display_output[40];
float first_num   = 0;
float second_num  = 0;
float result_flag = 0;
int   operation   = 0;
int   len         = 0;

/*
 * number_data — đúng thứ tự Proteus (đọc ngang từng hàng):
 *   index 0-3  : 7  8  9  /
 *   index 4-7  : 4  5  6  x
 *   index 8-11 : 1  2  3  -
 *   index 12-15: CLR 0  =  +
 */
char number_data[][10] = {
    "7","8","9","/",
    "4","5","6","x",
    "1","2","3","-",
    "CLEAR","0","=","+"
};

/*
 * numbers[] — giá trị số của từng index có chữ số
 * index:  0  1  2  (3=/)
 *         4  5  6  (7=x)
 *         8  9  10 (11=-)
 *        (12=CLR) 13 (14==) (15=+)
 */
int numbers[] = {
    7, 8, 9, 0,    /* 0-3  */
    4, 5, 6, 0,    /* 4-7  */
    1, 2, 3, 0,    /* 8-11 */
    0, 0, 0, 0     /* 12-15 (placeholder) */
};
/* numbers[13]=0 diễn giải là phím "0" vật lý */

/* ================= HELPER ================= */
void calc_reset(void){
    strcpy(display_output, "");
    len         = 0;
    operation   = 0;
    first_num   = 0;
    second_num  = 0;
    result_flag = 0;
}

/* ================= CALC: xử lý phím ================= */
/*
 * Phím số hợp lệ: 0(7) 1(8) 2(9) 4(4) 5(5) 6(6) 8(1) 9(2) 10(3) 13(0)
 * Phím =  : 14
 * Phím CLR: 12
 * Phép tính: 3(/) 7(x) 11(-) 15(+)
 */
int is_digit_key(int n){
    return (n==0||n==1||n==2||
            n==4||n==5||n==6||
            n==8||n==9||n==10||
            n==13);
}

int get_number(int n){
    if(n==13) return 0;      /* phím 0 vật lý */
    return numbers[n];
}

void display(int n){
    if(is_digit_key(n)){
        int val = get_number(n);

        if(!operation){
            /* --- đang nhập first_num --- */
            int has_minus = (len==1 && display_output[0]=='-') ? 1 : 0;
            float abs_val = (first_num < 0) ? -first_num : first_num;

            if(len==0 || (len==1 && has_minus)){
                abs_val = (float)val;
            } else {
                if(!result_flag){
                    abs_val = abs_val * 10.0f + (float)val;
                } else {
                    /* sau kết quả → bắt đầu phép mới */
                    calc_reset();
                    abs_val = (float)val;
                }
            }
            first_num = has_minus ? -abs_val : abs_val;
        } else {
            /* --- đang nhập second_num --- */
            int has_minus2 = (len==1 && display_output[strlen(display_output)-1]=='-') ? 1 : 0;
            float abs_val2 = (second_num < 0) ? -second_num : second_num;

            if(len==0 || (len==1 && has_minus2)){
                abs_val2 = (float)val;
            } else {
                abs_val2 = abs_val2 * 10.0f + (float)val;
            }
            second_num = has_minus2 ? -abs_val2 : abs_val2;
        }

        len++;
        char tmp[2]; tmp[0]='0'+val; tmp[1]='\0';
        strcat(display_output, tmp);

    } else if(n == 11){
        /* FIX: phím "-"
         * - len==0 và chưa có operation → dấu âm cho first_num
         * - len==0 và đã có operation   → dấu âm cho second_num
         * - len>0 và có operation       → phép trừ
         */
        if(len == 0){
            /* bật dấu âm cho số đang nhập */
            strcpy(display_output, "");
            if(!operation){
                first_num  = 0;
                strcat(display_output, "-");
            } else {
                /* giữ lại phần đã nhập trước đó (vd "12+") rồi thêm "-" */
                /* display_output hiện đang chứa "12+" rồi, thêm "-" */
                strcat(display_output, "-");
                second_num = 0;
            }
            len = 1;
            lcd_clear();
            lcd_gotoxy(0,0);
            lcd_puts(display_output);
            return;  /* không rơi xuống lcd_puts bên dưới */
        } else {
            /* len>0 → phép trừ bình thường */
            if(!result_flag){
                strcat(display_output, "-");
                operation = 11;
                len = 0;
            }
        }

    } else if(n == 14){
        /* = : tính kết quả */
        if(len){
            if(operation){
                switch(operation){
                    case 15: first_num = first_num + second_num; break; /* + */
                    case 11: first_num = first_num - second_num; break; /* - */
                    case 7:  first_num = first_num * second_num; break; /* x */
                    case 3:
                        /* FIX: chia cho 0 → hiện INF, chia bình thường bình thường */
                        if(second_num == 0){
                            strcpy(display_output, "INF");
                            operation   = 0;
                            result_flag = 1;
                            len         = 0;
                            lcd_clear();
                            lcd_gotoxy(0,0);
                            lcd_puts(display_output);
                            return;
                        } else {
                            first_num /= second_num;
                        }
                        break;
                }
                result_flag = 1;
            }
            operation = 0;
            strcpy(display_output, "");
            dtostrf(first_num, 0, dp, display_output);
            len = 0;
        }

    } else if(n == 12){
        /* CLR */
        calc_reset();

    } else if(n==3 || n==7 || n==15){
        /* phép tính: /  x  +  (phím "-" đã xử lý riêng ở trên) */
        if(len){
            strcat(display_output, number_data[n]);
            operation = n;
            len = 0;
        }
    }

    lcd_clear();
    lcd_gotoxy(0, 0);
    lcd_puts(display_output);
}

/* ================= MENU ================= */
int menu_mode  = 1;
int menu_index = 0;

void show_menu(){
    lcd_clear();
    if(menu_index == 0){
        lcd_puts(">Calc");
        lcd_gotoxy(0,1);
        lcd_puts(" Log");
    } else if(menu_index == 1){
        lcd_puts(">Log");
        lcd_gotoxy(0,1);
        lcd_puts(" PT2");
    } else {
        lcd_puts(">PT2");
        lcd_gotoxy(0,1);
        lcd_puts(" Calc");
    }
}

/* ================= MAIN ================= */
int main(void){
    lcd_init();
    key_init();
    show_menu();

    /* Biến PT2 */
    float a = 0, b = 0, c = 0;
    int   step = 0;

    /* Biến LOG */
    float log_base = 0;
    int   log_step = 0;   /* 0=nhập base, 1=nhập x */

    while(1){
        unsigned char key = key_scan();
        if(key == 0xFF) continue;

        /* ==============================================
         * MENU
         * Phím 9 (index 9 = phím "2" vật lý) = xuống
         * Phím 1 (index 1 = phím "8" vật lý) = lên
         * Phím 14 (=) = chọn
         * ============================================== */
        if(menu_mode == 1){
            if(key == 9){
                /* phím "2" → xuống */
                menu_index = (menu_index + 1) % 3;
                show_menu();
            }
            else if(key == 1){
                /* phím "8" → lên */
                menu_index = (menu_index == 0) ? 2 : menu_index - 1;
                show_menu();
            }
            else if(key == 14){
                /* phím "=" → chọn mode */
                menu_mode = menu_index + 2;
                lcd_clear();
                if(menu_mode == 3){
                    log_step = 0;
                    log_base = 0;
                    calc_reset();
                    lcd_puts("base=");
                } else if(menu_mode == 4){
                    step = 0;
                    a = 0; b = 0; c = 0;
                    calc_reset();
                    lcd_puts("a=");
                }
            }
        }

        /* ==============================================
         * CALC — máy tính 4 phép
         * Phím 12 (CLR/ON/C) = thoát
         * ============================================== */
        else if(menu_mode == 2){
            if(key == 12){
                calc_reset();
                menu_mode = 1;
                show_menu();
            } else {
                display(key);
            }
        }

        /* ==============================================
         * LOG — log_b(x) = ln(x)/ln(b)
         * Bước 0: nhập base → nhấn = xác nhận
         * Bước 1: nhập x   → nhấn = tính kết quả
         * Phím 11 (-) khi len==0 → số âm
         * Phím 12 (CLR) → thoát
         * ============================================== */
        else if(menu_mode == 3){

            if(key == 12){
                /* CLR: thoát */
                calc_reset();
                log_base = 0;
                log_step = 0;
                menu_mode = 1;
                show_menu();
            }
            else if(is_digit_key(key)){
                int   has_minus = (display_output[0] == '-') ? 1 : 0;
                float abs_val   = (first_num < 0) ? -first_num : first_num;
                int   val       = get_number(key);

                if(len == 0 || (len == 1 && has_minus)){
                    abs_val = (float)val;
                } else {
                    abs_val = abs_val * 10.0f + (float)val;
                }
                first_num = has_minus ? -abs_val : abs_val;
                len++;

                char tmp[2]; tmp[0]='0'+val; tmp[1]='\0';
                strcat(display_output, tmp);

                lcd_clear();
                if(log_step == 0){ lcd_puts("base="); lcd_gotoxy(5,0); }
                else              { lcd_puts("x=");    lcd_gotoxy(2,0); }
                lcd_puts(display_output);
            }
            else if(key == 11 && len == 0){
                /* phím "-" khi chưa nhập → bật số âm */
                first_num = 0;
                strcpy(display_output, "-");
                len = 1;
                lcd_clear();
                if(log_step == 0) lcd_puts("base=-");
                else              lcd_puts("x=-");
            }
            else if(key == 14){
                /* phím "=" */
                if(log_step == 0){
                    /* xác nhận base */
                    if(first_num <= 0.0f ||
                       (first_num > 0.9999f && first_num < 1.0001f)){
                        lcd_clear();
                        lcd_puts("base err!");
                        lcd_gotoxy(0,1);
                        lcd_puts("b>0 & b!=1");
                        _delay_ms(1500);
                        calc_reset();
                        lcd_clear();
                        lcd_puts("base=");
                    } else {
                        log_base = first_num;
                        calc_reset();
                        log_step = 1;
                        lcd_clear();
                        lcd_puts("x=");
                    }
                } else {
                    /* tính kết quả */
                    if(first_num <= 0.0f){
                        lcd_clear();
                        lcd_puts("x err!");
                        lcd_gotoxy(0,1);
                        lcd_puts("x must be >0");
                        _delay_ms(1500);
                        calc_reset();
                        lcd_clear();
                        lcd_puts("x=");
                    } else {
                        float result = log(first_num) / log(log_base);
                        char  b_str[10];
                        dtostrf(log_base, 0, 2, b_str);

                        lcd_clear();
                        lcd_puts("log");
                        lcd_puts(b_str);
                        lcd_puts("(x)=");
                        lcd_gotoxy(0,1);
                        dtostrf(result, 0, 4, display_output);
                        lcd_puts(display_output);

                        calc_reset();
                        log_step = 0;
                        log_base = 0;
                        _delay_ms(2000);
                        lcd_clear();
                        lcd_puts("base=");
                    }
                }
            }
        }

        /* ==============================================
         * PT BẬC 2: ax^2 + bx + c = 0
         * Phím 3  (/)  = NEXT (chuyển a→b→c)
         * Phím 11 (-)  khi len==0 = số âm
         * Phím 14 (=)  = tính nghiệm
         * Phím 12 (CLR)= thoát
         * ============================================== */
        else if(menu_mode == 4){

            if(key == 12){
                /* CLR: reset hoàn toàn rồi thoát */
                calc_reset();
                a = 0; b = 0; c = 0;
                step = 0;
                menu_mode = 1;
                show_menu();
            }
            else if(is_digit_key(key)){
                int   has_minus = (display_output[0] == '-') ? 1 : 0;
                float abs_val   = (first_num < 0) ? -first_num : first_num;
                int   val       = get_number(key);

                if(len == 0 || (len == 1 && has_minus)){
                    abs_val = (float)val;
                } else {
                    abs_val = abs_val * 10.0f + (float)val;
                }
                first_num = has_minus ? -abs_val : abs_val;
                len++;

                if(step == 0)      a = first_num;
                else if(step == 1) b = first_num;
                else if(step == 2) c = first_num;

                char tmp[2]; tmp[0]='0'+val; tmp[1]='\0';
                strcat(display_output, tmp);

                lcd_clear();
                if(step == 0)      lcd_puts("a=");
                else if(step == 1) lcd_puts("b=");
                else               lcd_puts("c=");
                lcd_gotoxy(2,0);
                lcd_puts(display_output);
            }
            else if(key == 11 && len == 0){
                /* phím "-" khi chưa nhập → số âm */
                first_num = 0;
                strcpy(display_output, "-");
                len = 1;
                lcd_clear();
                if(step == 0)      lcd_puts("a=-");
                else if(step == 1) lcd_puts("b=-");
                else               lcd_puts("c=-");
            }
            else if(key == 3){
                /* phím "/" = NEXT */
                if(step == 0){
                    if(a == 0.0f){
                        lcd_clear();
                        lcd_puts("a != 0 !");
                        _delay_ms(1500);
                        lcd_clear();
                        lcd_puts("a=");
                        calc_reset();
                    } else {
                        step = 1;
                        calc_reset();
                        lcd_clear();
                        lcd_puts("b=");
                    }
                } else if(step == 1){
                    step = 2;
                    calc_reset();
                    lcd_clear();
                    lcd_puts("c=");
                }
                /* step==2: NEXT không làm gì */
            }
            else if(key == 14){
                /* phím "=" = tính nghiệm */
                if(step < 2){
                    lcd_clear();
                    lcd_puts("Need a,b,c");
                    lcd_gotoxy(0,1);
                    lcd_puts("Use / =next");
                    _delay_ms(1500);
                    lcd_clear();
                    if(step == 0) lcd_puts("a=");
                    else          lcd_puts("b=");
                } else {
                    c = first_num;
                    float delta = b*b - 4.0f*a*c;
                    lcd_clear();

                    if(delta < 0.0f){
                        lcd_puts("No real root");
                    } else if(delta == 0.0f){
                        float x0 = -b / (2.0f * a);
                        lcd_puts("x=");
                        dtostrf(x0, 0, 3, display_output);
                        lcd_puts(display_output);
                    } else {
                        float sq = sqrt(delta);
                        float x1 = (-b + sq) / (2.0f * a);
                        float x2 = (-b - sq) / (2.0f * a);

                        lcd_puts("x1=");
                        dtostrf(x1, 0, 3, display_output);
                        lcd_puts(display_output);

                        lcd_gotoxy(0,1);
                        lcd_puts("x2=");
                        dtostrf(x2, 0, 3, display_output);
                        lcd_puts(display_output);
                    }

                    step = 0;
                    a = 0; b = 0; c = 0;
                    calc_reset();
                    _delay_ms(2000);
                    lcd_clear();
                    lcd_puts("a=");
                }
            }
        }

    } /* while(1) */
}
