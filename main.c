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
    PORTA = 0xFF;   /* pullup t?t c? */
}

unsigned char key_scan(){
    unsigned char cnt, key;

    for(cnt=0; cnt<4; cnt++){
        PORTA = 0xFF;
        PORTA &= ~(1<<cnt);         /* kéo th?p t?ng c?t */
        key = PINA & 0xF0;          /* ??c 4 hàng (PA4-PA7) */

        if(key != 0xF0){
            while((PINA & 0xF0) != 0xF0); /* ch? nh? phím */
            break;
        }
    }

    if(cnt == 4) return 0xFF;       /* không có phím */

    /* Proteus KEYPAD-SMALLCALC — quét ngang theo hàng:
     *   cnt=0 (Col0): 7,4,1,CLR  ? key=0,4,8,12
     *   cnt=1 (Col1): 8,5,2,0    ? key=1,5,9,13
     *   cnt=2 (Col2): 9,6,3,=    ? key=2,6,10,14
     *   cnt=3 (Col3): /,x,-,+    ? key=3,7,11,15
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

/* ================= BI?N TOÀN C?C ================= */
#define dp 3

char  display_output[40];
float first_num   = 0;
float second_num  = 0;
float result_flag = 0;
int   operation   = 0;
int   len         = 0;
int trig_func=0;
/*
 * number_data — ?úng th? t? Proteus (??c ngang t?ng hàng):
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
 * numbers[] — giá tr? s? c?a t?ng index có ch? s?
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
/* numbers[13]=0 di?n gi?i là phím "0" v?t lý */

/* ================= HELPER ================= */
void calc_reset(void){
    strcpy(display_output, "");
    len         = 0;
    operation   = 0;
    first_num   = 0;
    second_num  = 0;
    result_flag = 0;
}

/* ================= CALC: x? lý phím ================= */
/*
 * Phím s? h?p l?: 0(7) 1(8) 2(9) 4(4) 5(5) 6(6) 8(1) 9(2) 10(3) 13(0)
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
    if(n==13) return 0;      /* phím 0 v?t lý */
    return numbers[n];
}

void display(int n){
    if(is_digit_key(n)){
        int val = get_number(n);

        if(!operation){
            /* --- ?ang nh?p first_num --- */
            int has_minus = (len==1 && display_output[0]=='-') ? 1 : 0;
            float abs_val = (first_num < 0) ? -first_num : first_num;

            if(len==0 || (len==1 && has_minus)){
                abs_val = (float)val;
            } else {
                if(!result_flag){
                    abs_val = abs_val * 10.0f + (float)val;
                } else {
                    /* sau k?t qu? ? b?t ??u phép m?i */
                    calc_reset();
                    abs_val = (float)val;
                }
            }
            first_num = has_minus ? -abs_val : abs_val;
        } else {
            /* --- ?ang nh?p second_num --- */
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
         * - len==0 và ch?a có operation ? d?u âm cho first_num
         * - len==0 và ?ã có operation   ? d?u âm cho second_num
         * - len>0 và có operation       ? phép tr?
         */
        if(len == 0){
            /* b?t d?u âm cho s? ?ang nh?p */
            strcpy(display_output, "");
            if(!operation){
                first_num  = 0;
                strcat(display_output, "-");
            } else {
                /* gi? l?i ph?n ?ã nh?p tr??c ?ó (vd "12+") r?i thêm "-" */
                /* display_output hi?n ?ang ch?a "12+" r?i, thêm "-" */
                strcat(display_output, "-");
                second_num = 0;
            }
            len = 1;
            lcd_clear();
            lcd_gotoxy(0,0);
            lcd_puts(display_output);
            return;  /* không r?i xu?ng lcd_puts bên d??i */
        } else {
            /* len>0 ? phép tr? bình th??ng */
            if(!result_flag){
                strcat(display_output, "-");
                operation = 11;
                len = 0;
            }
        }

    } else if(n == 14){
        /* = : tính k?t qu? */
        if(len){
            if(operation){
                switch(operation){
                    case 15: first_num = first_num + second_num; break; /* + */
                    case 11: first_num = first_num - second_num; break; /* - */
                    case 7:  first_num = first_num * second_num; break; /* x */
                    case 3:
                        /* FIX: chia cho 0 ? hi?n INF, chia bình th??ng bình th??ng */
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
        /* phép tính: /  x  +  (phím "-" ?ã x? lý riêng ? trên) */
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
	if(menu_index==0){
		lcd_puts(">Calc");
		lcd_gotoxy(0,1); lcd_puts(" Log");
		}else if(menu_index==1){
		lcd_puts(">Log");
		lcd_gotoxy(0,1); lcd_puts(" PT2");
		}else if(menu_index==2){
		lcd_puts(">PT2");
		lcd_gotoxy(0,1); lcd_puts(" Trig");
		}else{
		lcd_puts(">Trig");
		lcd_gotoxy(0,1); lcd_puts(" Calc");
	}
}

void show_trig_menu(){
	lcd_clear();
	if(trig_func==0){
		lcd_puts(">sin");
		lcd_gotoxy(0,1); lcd_puts(" cos");
		}else if(trig_func==1){
		lcd_puts(">cos");
		lcd_gotoxy(0,1); lcd_puts(" tan");
		}else if(trig_func==2){
		lcd_puts(">tan");
		lcd_gotoxy(0,1); lcd_puts(" cot");
		}else{
		lcd_puts(">cot");
		lcd_gotoxy(0,1); lcd_puts(" sin");
	}
}


/* ================================================================
 * CALC — MÁY TÍNH CHUỖI CÓ ƯU TIÊN TOÁN TỬ
 *
 * Thuật toán: Two-stack (giá trị + toán tử) — xử lý đúng thứ tự
 *   x/ ưu tiên cao hơn +-
 *   Ví dụ: 2+3x4= → 2+(3x4) = 14  ✅
 *           6/2+1=  → (6/2)+1 = 4  ✅
 *
 * Giới hạn stack: tối đa 8 số hạng liên tiếp (đủ cho LCD 16 ký tự)
 *
 * Phím:
 *   Số (is_digit_key) → nhập số hiện tại
 *   "-"(11) khi cur_len==0 → số âm
 *   "-"(11) khi cur_len>0  → phép trừ (push vào stack)
 *   +/x/ (15/7/3)          → push vào stack
 *   "="(14)                → tính toàn bộ stack → hiện kết quả
 *   CLR(12)                → xóa sạch
 * ================================================================ */

#define STACK_SIZE 8

/* Stack số hạng và toán tử */
float val_stack[STACK_SIZE];
int   op_stack[STACK_SIZE];   /* 0=none, 3=/ 7=x 11=- 15=+ */
int   val_top = 0;
int   op_top  = 0;

float cur_val   = 0;    /* số đang nhập */
int   cur_len   = 0;    /* số ký tự đã nhập vào số hiện tại */
int   has_minus = 0;    /* số hiện tại có dấu âm không */
int   calc_done = 0;    /* đã nhấn = và có kết quả */
float last_result = 0;  /* kết quả lần trước (để tiếp tục tính) */

char  expr_buf[33];     /* chuỗi hiển thị biểu thức (LCD 16 ký tự x 2 dòng) */

void calc_full_reset(){
    val_top=0; op_top=0;
    cur_val=0; cur_len=0; has_minus=0;
    calc_done=0; last_result=0;
    strcpy(expr_buf,"");
}

/* Độ ưu tiên toán tử */
int precedence(int op){
    if(op==7 || op==3) return 2;   /* x, / */
    if(op==15|| op==11) return 1;  /* +, - */
    return 0;
}

/* Thực hiện 1 phép tính: val_stack[i-1] op val_stack[i] */
float apply_op(float a, int op, float b){
    switch(op){
        case 15: return a + b;
        case 11: return a - b;
        case 7:  return a * b;
        case 3:
            if(b==0.0f) return 1e30f;  /* chia 0 → số rất lớn, hiện INF */
            return a / b;
    }
    return b;
}

/* Tính toàn bộ stack (có ưu tiên) — trả về kết quả cuối */
float calc_evaluate(){
    /* Đẩy cur_val vào stack trước */
    if(val_top < STACK_SIZE){
        val_stack[val_top++] = has_minus ? -cur_val : cur_val;
    }

    /* Bước 1: xử lý tất cả phép x/ trước */
    int   vt = 0, ot = 0;
    float tmp_val[STACK_SIZE];
    int   tmp_op[STACK_SIZE];
    int   i;

    tmp_val[vt++] = val_stack[0];
    for(i=0; i<op_top; i++){
        if(op_stack[i]==7 || op_stack[i]==3){
            /* x hoặc / — tính ngay */
            float res = apply_op(tmp_val[vt-1], op_stack[i], val_stack[i+1]);
            tmp_val[vt-1] = res;
        } else {
            /* + hoặc - — để lại cho bước 2 */
            tmp_op[ot++] = op_stack[i];
            tmp_val[vt++] = val_stack[i+1];
        }
    }

    /* Bước 2: xử lý tất cả +- từ trái sang phải */
    float result = tmp_val[0];
    for(i=0; i<ot; i++){
        result = apply_op(result, tmp_op[i], tmp_val[i+1]);
    }

    return result;
}

/* Tên ký hiệu toán tử để hiển thị */
char op_char(int op){
    if(op==15) return '+';
    if(op==11) return '-';
    if(op==7)  return 'x';
    if(op==3)  return '/';
    return '?';
}

/* Cập nhật LCD với biểu thức đang nhập */
void calc_refresh(){
    /* Dòng 0: biểu thức (tối đa 16 ký tự, cuộn nếu dài hơn) */
    char line0[17]={0};
    int  elen = (int)strlen(expr_buf);
    if(elen<=16){
        strncpy(line0, expr_buf, 16);
    } else {
        /* Hiện 16 ký tự cuối */
        strncpy(line0, expr_buf+elen-16, 16);
    }

    /* Dòng 1: số đang nhập */
    char line1[17]={0};
    if(calc_done){
        /* Hiện kết quả ở dòng 1 */
        dtostrf(last_result, 0, 3, line1);
        if(last_result > 1e29f) strcpy(line1,"INF");
    } else if(cur_len>0){
        if(has_minus) strcpy(line1,"-");
        char num_str[12]; dtostrf(cur_val,0,0,num_str);
        if(has_minus) strcat(line1,num_str); else strcpy(line1,num_str);
    } else {
        strcpy(line1,"_");
    }

    lcd_clear();
    lcd_gotoxy(0,0); lcd_puts(line0);
    lcd_gotoxy(0,1); lcd_puts(line1);
}

/* Xử lý phím trong mode CALC */
void calc_handle(unsigned char key){

    /* CLR: reset hoàn toàn */
    if(key==12){
        calc_full_reset();
        lcd_clear(); lcd_gotoxy(0,0); lcd_puts("0");
        return;
    }

    /* Nếu vừa có kết quả và nhấn số → bắt đầu biểu thức mới */
    if(calc_done && is_digit_key(key)){
        calc_full_reset();
    }
    /* Nếu vừa có kết quả và nhấn toán tử → tiếp tục từ kết quả */
    if(calc_done && (key==3||key==7||key==11||key==15)){
        calc_full_reset();
        cur_val   = last_result;
        cur_len   = 1;
        has_minus = (last_result < 0) ? 1 : 0;
        char tmp[12]; dtostrf(last_result<0?-last_result:last_result,0,3,tmp);
        strcat(expr_buf, tmp);
    }

    /* --- Phím số --- */
    if(is_digit_key(key)){
        int val = get_number(key);
        if(cur_len==0){
            cur_val = (float)val;
        } else {
            cur_val = cur_val*10.0f + (float)val;
        }
        cur_len++;
        /* Thêm ký tự vào expr_buf */
        char tmp[2]; tmp[0]='0'+val; tmp[1]='\0';
        if(strlen(expr_buf)<32) strcat(expr_buf,tmp);
        calc_refresh();
    }

    /* --- Phím "-" --- */
    else if(key==11){
        if(cur_len==0 && val_top==0 && op_top==0){
            /* Số đầu tiên âm */
            has_minus=1;
            if(strlen(expr_buf)<32) strcat(expr_buf,"-");
            calc_refresh();
        } else if(cur_len==0 && op_top>0){
            /* Số âm sau toán tử */
            has_minus=1;
            if(strlen(expr_buf)<32) strcat(expr_buf,"-");
            calc_refresh();
        } else if(cur_len>0){
            /* Phép trừ — push số hiện tại vào stack */
            if(val_top<STACK_SIZE && op_top<STACK_SIZE){
                val_stack[val_top++] = has_minus ? -cur_val : cur_val;
                op_stack[op_top++]   = 11;
                cur_val=0; cur_len=0; has_minus=0;
                if(strlen(expr_buf)<32) strcat(expr_buf,"-");
                calc_refresh();
            }
        }
    }

    /* --- Phím toán tử +, x, / --- */
    else if(key==15||key==7||key==3){
        if(cur_len>0 && val_top<STACK_SIZE && op_top<STACK_SIZE){
            val_stack[val_top++] = has_minus ? -cur_val : cur_val;
            op_stack[op_top++]   = key;
            cur_val=0; cur_len=0; has_minus=0;
            char sym[2]; sym[0]=op_char(key); sym[1]='\0';
            if(strlen(expr_buf)<32) strcat(expr_buf,sym);
            calc_refresh();
        }
    }

    /* --- Phím "=" → tính kết quả --- */
    else if(key==14){
        if(cur_len==0 && op_top==0) return;  /* chưa nhập gì */
        if(cur_len==0) return;                /* nhập toán tử nhưng chưa có số sau */

        float res = calc_evaluate();
        last_result = res;
        calc_done   = 1;

        /* Hiện: dòng 0 = biểu thức+"=", dòng 1 = kết quả */
        char line0[17]={0};
        int  elen=(int)strlen(expr_buf);
        if(elen<=15){
            strncpy(line0,expr_buf,15);
            strcat(line0,"=");
        } else {
            strncpy(line0,expr_buf+elen-15,15);
            strcat(line0,"=");
        }

        char line1[17]={0};
        if(res>1e29f) strcpy(line1,"INF");
        else dtostrf(res,0,3,line1);

        lcd_clear();
        lcd_gotoxy(0,0); lcd_puts(line0);
        lcd_gotoxy(0,1); lcd_puts(line1);

        /* Reset stack, giữ last_result */
        val_top=0; op_top=0;
        cur_val=0; cur_len=0; has_minus=0;
        strcpy(expr_buf,"");
    }
}

/* ================================================================
 * HELPER LOG & PT2 (dùng first_num, len riêng)
 * ================================================================ */
char  log_buf[12];
float log_first  = 0;
int   log_len    = 0;

void log_reset(){
    strcpy(log_buf,""); log_first=0; log_len=0;
}

/* Nhập số cho LOG và PT2 — trả về giá trị hiện tại */
void generic_input(char *buf, float *val, int *vlen, int key){
    int   has_m  = (*vlen==1 && buf[0]=='-') ? 1 : 0;
    float abs_v  = (*val<0) ? -(*val) : *val;
    int   digit  = get_number(key);

    if(*vlen==0 || (*vlen==1 && has_m)){
        abs_v = (float)digit;
    } else {
        abs_v = abs_v*10.0f + (float)digit;
    }
    *val = has_m ? -abs_v : abs_v;
    (*vlen)++;
    char tmp[2]; tmp[0]='0'+digit; tmp[1]='\0';
    strcat(buf, tmp);
}

/* ================================================================
 * MAIN
 * ================================================================ */
int main(void){
    lcd_init();
    key_init();
    show_menu();

    /* Biến PT2 */
    float pt_a=0, pt_b=0, pt_c=0;
    int   step=0;
    char  pt_buf[12]; strcpy(pt_buf,"");
    float pt_cur=0;
    int   pt_len=0;

    /* Biến LOG */
    float log_base=0;
    int   log_step=0;
    char  lb_buf[12]; strcpy(lb_buf,"");
    float lb_val=0;
    int   lb_len=0;

    /* Biến TRIG */
    static int trig_step=0;
    char  trig_buf[12]; strcpy(trig_buf,"");
    float trig_angle=0;
    int   trig_len=0;

    while(1){
        unsigned char key = key_scan();
        if(key==0xFF) continue;

        /* ============================================================
         * MENU CHÍNH
         * ============================================================ */
        if(menu_mode==1){
            if(key==9){
                menu_index=(menu_index+1)%4; show_menu();
            }
            else if(key==1){
                menu_index=(menu_index==0)?3:menu_index-1; show_menu();
            }
            else if(key==14){
                menu_mode=menu_index+2;
                lcd_clear();
                if(menu_mode==2){
                    /* Calc */
                    calc_full_reset();
                    lcd_gotoxy(0,0); lcd_puts("0");
                } else if(menu_mode==3){
                    /* Log */
                    log_step=0; log_base=0;
                    log_reset(); strcpy(lb_buf,""); lb_val=0; lb_len=0;
                    lcd_puts("base=");
                } else if(menu_mode==4){
                    /* PT2 */
                    step=0; pt_a=0; pt_b=0; pt_c=0;
                    strcpy(pt_buf,""); pt_cur=0; pt_len=0;
                    lcd_puts("a=");
                } else if(menu_mode==5){
                    /* Trig */
                    trig_func=0; trig_step=0;
                    strcpy(trig_buf,""); trig_angle=0; trig_len=0;
                    show_trig_menu();
                }
            }
        }

        /* ============================================================
         * CALC — chuỗi phép tính, ưu tiên x/ trước +-
         *
         * Ví dụ đúng:
         *   2+3x4=    → 14   (3x4=12, rồi 2+12)
         *   10-2x3=   → 4    (2x3=6,  rồi 10-6)
         *   8/2+1=    → 5    (8/2=4,  rồi 4+1)
         *   -5+3=     → -2
         *   3x-2=     → -6   (nhấn x rồi nhấn - rồi nhấn 2)
         *
         * CLR(12) = xóa sạch
         * ============================================================ */
        else if(menu_mode==2){
            if(key==12 && !calc_done){
                /* CLR khi chưa có kết quả → về menu */
                calc_full_reset();
                menu_mode=1; show_menu();
            } else {
                calc_handle(key);
            }
        }

        /* ============================================================
         * LOG — log_b(x) = ln(x)/ln(b)
         *
         * Bước 0: nhập base → "="(14) xác nhận
         * Bước 1: nhập x   → "="(14) tính
         * "-"(11) khi len==0 → số âm
         * CLR(12) → về menu
         * ============================================================ */
        else if(menu_mode==3){

            if(key==12){
                log_step=0; log_base=0;
                strcpy(lb_buf,""); lb_val=0; lb_len=0;
                menu_mode=1; show_menu();
            }
            else if(is_digit_key(key)){
                generic_input(lb_buf, &lb_val, &lb_len, key);
                lcd_clear();
                if(log_step==0){ lcd_puts("base="); lcd_gotoxy(5,0); }
                else            { lcd_puts("x=");    lcd_gotoxy(2,0); }
                lcd_puts(lb_buf);
            }
            else if(key==11 && lb_len==0){
                lb_val=0; strcpy(lb_buf,"-"); lb_len=1;
                lcd_clear();
                if(log_step==0) lcd_puts("base=-");
                else            lcd_puts("x=-");
            }
            else if(key==14){
                if(log_step==0){
                    if(lb_val<=0.0f||(lb_val>0.9999f&&lb_val<1.0001f)){
                        lcd_clear(); lcd_puts("base err!");
                        lcd_gotoxy(0,1); lcd_puts("b>0 & b!=1");
                        _delay_ms(1500);
                        strcpy(lb_buf,""); lb_val=0; lb_len=0;
                        lcd_clear(); lcd_puts("base=");
                    } else {
                        log_base=lb_val;
                        strcpy(lb_buf,""); lb_val=0; lb_len=0;
                        log_step=1; lcd_clear(); lcd_puts("x=");
                    }
                } else {
                    if(lb_val<=0.0f){
                        lcd_clear(); lcd_puts("x err!");
                        lcd_gotoxy(0,1); lcd_puts("x must be >0");
                        _delay_ms(1500);
                        strcpy(lb_buf,""); lb_val=0; lb_len=0;
                        lcd_clear(); lcd_puts("x=");
                    } else {
                        float res=(float)(log((double)lb_val)/log((double)log_base));
                        char b_str[10]; dtostrf(log_base,0,2,b_str);
                        lcd_clear();
                        lcd_puts("log"); lcd_puts(b_str); lcd_puts("(x)=");
                        lcd_gotoxy(0,1);
                        char res_str[12]; dtostrf(res,0,4,res_str);
                        lcd_puts(res_str);
                        strcpy(lb_buf,""); lb_val=0; lb_len=0;
                        log_step=0; log_base=0;
                        _delay_ms(2000);
                        lcd_clear(); lcd_puts("base=");
                    }
                }
            }
        }

        /* ============================================================
         * PT BẬC 2 — ax² + bx + c = 0
         *
         * "/"(3)  = NEXT (a→b→c)
         * "-"(11) khi pt_len==0 = số âm
         * "="(14) = tính nghiệm
         * CLR(12) = về menu
         * ============================================================ */
        else if(menu_mode==4){

            if(key==12){
                step=0; pt_a=0; pt_b=0; pt_c=0;
                strcpy(pt_buf,""); pt_cur=0; pt_len=0;
                menu_mode=1; show_menu();
            }
            else if(is_digit_key(key)){
                generic_input(pt_buf, &pt_cur, &pt_len, key);
                if(step==0)      pt_a=pt_cur;
                else if(step==1) pt_b=pt_cur;
                else if(step==2) pt_c=pt_cur;

                lcd_clear();
                if(step==0)      lcd_puts("a=");
                else if(step==1) lcd_puts("b=");
                else             lcd_puts("c=");
                lcd_gotoxy(2,0); lcd_puts(pt_buf);
            }
            else if(key==11 && pt_len==0){
                pt_cur=0; strcpy(pt_buf,"-"); pt_len=1;
                lcd_clear();
                if(step==0)      lcd_puts("a=-");
                else if(step==1) lcd_puts("b=-");
                else             lcd_puts("c=-");
            }
            else if(key==3){
                /* NEXT */
                if(step==0){
                    if(pt_a==0.0f){
                        lcd_clear(); lcd_puts("a != 0 !");
                        _delay_ms(1500);
                        strcpy(pt_buf,""); pt_cur=0; pt_len=0;
                        lcd_clear(); lcd_puts("a=");
                    } else {
                        step=1; strcpy(pt_buf,""); pt_cur=0; pt_len=0;
                        lcd_clear(); lcd_puts("b=");
                    }
                } else if(step==1){
                    step=2; strcpy(pt_buf,""); pt_cur=0; pt_len=0;
                    lcd_clear(); lcd_puts("c=");
                }
            }
            else if(key==14){
                if(step<2){
                    lcd_clear(); lcd_puts("Need a,b,c");
                    lcd_gotoxy(0,1); lcd_puts("Use / = next");
                    _delay_ms(1500);
                    lcd_clear();
                    if(step==0) lcd_puts("a="); else lcd_puts("b=");
                } else {
                    pt_c=pt_cur;
                    float delta=pt_b*pt_b-4.0f*pt_a*pt_c;
                    lcd_clear();
                    char tmp[12];
                    if(delta<0.0f){
                        lcd_puts("No real root");
                    } else if(delta==0.0f){
                        float x0=-pt_b/(2.0f*pt_a);
                        lcd_puts("x="); dtostrf(x0,0,3,tmp); lcd_puts(tmp);
                    } else {
                        float sq=(float)sqrt((double)delta);
                        float x1=(-pt_b+sq)/(2.0f*pt_a);
                        float x2=(-pt_b-sq)/(2.0f*pt_a);
                        lcd_puts("x1="); dtostrf(x1,0,3,tmp); lcd_puts(tmp);
                        lcd_gotoxy(0,1);
                        lcd_puts("x2="); dtostrf(x2,0,3,tmp); lcd_puts(tmp);
                    }
                    step=0; pt_a=0; pt_b=0; pt_c=0;
                    strcpy(pt_buf,""); pt_cur=0; pt_len=0;
                    _delay_ms(2000);
                    lcd_clear(); lcd_puts("a=");
                }
            }
        }

        /* ============================================================
         * TRIG — sin/cos/tan/cot (góc theo độ)
         *
         * trig_step=0: menu chọn hàm
         *   "8"(1)="lên", "2"(9)=xuống, "="(14)=chọn
         *
         * trig_step=1: nhập góc
         *   chữ số → nhập góc
         *   "-"(11) khi trig_len==0 → góc âm
         *   "="(14) → tính
         *   CLR(12) → về menu trig
         *   "x"(7)  → về menu chính
         *
         * tan(90+180k°) hoặc cot(0+180k°) → "INF"
         * ============================================================ */
        else if(menu_mode==5){

            if(key==7){
                /* "x" = về menu chính */
                trig_step=0; trig_func=0;
                strcpy(trig_buf,""); trig_angle=0; trig_len=0;
                menu_mode=1; show_menu();
            }
            else if(key==12){
                if(trig_step==1){
                    /* CLR khi nhập góc → về menu trig */
                    strcpy(trig_buf,""); trig_angle=0; trig_len=0;
                    trig_step=0; show_trig_menu();
                } else {
                    /* CLR ở menu trig → về menu chính */
                    trig_step=0; trig_func=0;
                    menu_mode=1; show_menu();
                }
            }

            /* ---- Menu chọn hàm ---- */
            else if(trig_step==0){
                if(key==1){
                    trig_func=(trig_func==0)?3:trig_func-1;
                    show_trig_menu();
                }
                else if(key==9){
                    trig_func=(trig_func+1)%4;
                    show_trig_menu();
                }
                else if(key==14){
                    trig_step=1;
                    strcpy(trig_buf,""); trig_angle=0; trig_len=0;
                    lcd_clear();
                    if(trig_func==0)      lcd_puts("sin(deg)=");
                    else if(trig_func==1) lcd_puts("cos(deg)=");
                    else if(trig_func==2) lcd_puts("tan(deg)=");
                    else                  lcd_puts("cot(deg)=");
                }
            }

            /* ---- Nhập góc ---- */
            else {
                if(is_digit_key(key)){
                    generic_input(trig_buf, &trig_angle, &trig_len, key);
                    lcd_clear();
                    if(trig_func==0)      lcd_puts("sin(");
                    else if(trig_func==1) lcd_puts("cos(");
                    else if(trig_func==2) lcd_puts("tan(");
                    else                  lcd_puts("cot(");
                    lcd_puts(trig_buf); lcd_puts(")");
                }
                else if(key==11 && trig_len==0){
                    trig_angle=0; strcpy(trig_buf,"-"); trig_len=1;
                    lcd_clear();
                    if(trig_func==0)      lcd_puts("sin(-");
                    else if(trig_func==1) lcd_puts("cos(-");
                    else if(trig_func==2) lcd_puts("tan(-");
                    else                  lcd_puts("cot(-");
                }
                else if(key==14){
                    if(trig_len==0){
                        lcd_clear(); lcd_puts("Enter angle!");
                        _delay_ms(1500);
                        lcd_clear();
                        if(trig_func==0)      lcd_puts("sin(deg)=");
                        else if(trig_func==1) lcd_puts("cos(deg)=");
                        else if(trig_func==2) lcd_puts("tan(deg)=");
                        else                  lcd_puts("cot(deg)=");
                    } else {
                        double rad = (double)trig_angle * M_PI / 180.0;
                        double res = 0.0;
                        int    is_inf=0;
                        char   res_str[12];

                        switch(trig_func){
                            case 0: res=sin(rad); break;
                            case 1: res=cos(rad); break;
                            case 2:
                                if(fabs(cos(rad))<1e-6) is_inf=1;
                                else res=tan(rad);
                                break;
                            case 3:
                                if(fabs(sin(rad))<1e-6) is_inf=1;
                                else res=cos(rad)/sin(rad);
                                break;
                        }

                        lcd_clear();
                        if(trig_func==0)      lcd_puts("sin(");
                        else if(trig_func==1) lcd_puts("cos(");
                        else if(trig_func==2) lcd_puts("tan(");
                        else                  lcd_puts("cot(");
                        lcd_puts(trig_buf); lcd_puts(")=");

                        lcd_gotoxy(0,1);
                        if(is_inf){
                            lcd_puts("INF");
                        } else {
                            dtostrf((float)res,0,4,res_str);
                            lcd_puts(res_str);
                        }

                        strcpy(trig_buf,""); trig_angle=0; trig_len=0;
                        trig_step=0;
                        _delay_ms(2500);
                        show_trig_menu();
                    }
                }
            }
        }

    } /* while(1) */
}
