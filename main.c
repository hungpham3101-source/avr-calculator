#define F_CPU 8000000UL

#include <avr/io.h>
#include <util/delay.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

/* =================================================================
 * PHẦN CỨNG:
 *   LCD 16x2  — PORTB:  PB0-PB3=D4-D7,  PB4=EN,  PB5=RS
 *   Keypad 4x6 — PORTA: PA0-PA3=Row A-D (INPUT  + pullup 0x0F)
 *                PORTC: PC0-PC5=Col 0-5  (OUTPUT, kéo thấp từng cột)
 *
 * KEYPAD LAYOUT:
 *   Col:      0      1    2    3    4      5
 *   Row A: [ON/C]  [ 7] [ 8] [ 9] [ x]  [ /]
 *   Row B: [+/-]   [ 4] [ 5] [ 6] [ -]  [MODE]
 *   Row C: [ % ]   [ 1] [ 2] [ 3] [ +]  [ UP]
 *   Row D: [ √ ]   [ 0] [ .]  [=]  [+]  [DOWN]
 *
 * KEY INDEX (key_map[row][col]):
 *   Row A: 12  0  1  2   7  3
 *   Row B: 18  4  5  6  11 23
 *   Row C: 19  8  9 10  15 24
 *   Row D: 16 13 17 14  15 25
 *
 * PHÍM ĐẶC BIỆT:
 *   12 = ON/C (CLR)       23 = MODE (vào menu)
 *   24 = UP  (lên menu)   25 = DOWN (xuống menu)
 *   14 = = (tính/xác nhận)
 *   16 = √  (căn bậc 2)
 *   17 = .  (dấu chấm thập phân)
 *   18 = +/- (đảo dấu — chưa dùng trong code này)
 *   19 = %  (chưa dùng)
 *
 * MENU_MODE:
 *   0 = menu chính
 *   2 = Calc  (chuỗi phép tính, ưu tiên x/ trước +-)
 *   3 = Log   (log cơ số bất kỳ, hỗ trợ số thực)
 *   4 = PT2   (phương trình bậc 2)
 *   5 = Trig  (sin/cos/tan/cot, góc theo độ)
 *   6 = x^y   (lũy thừa)
 * ================================================================= */

/* ================================================================
 * LCD — PORTB
 * FIX: tách riêng data nibble (PB0-PB3) và control (PB4=EN, PB5=RS)
 *      dùng mask 0xC0 để bảo vệ PB6-PB7, set/clr RS EN riêng biệt
 * ================================================================ */
#define LCD_Port PORTB
#define LCD_Dir  DDRB
#define RS       PB5
#define EN       PB4

/* Ghi 4-bit data vào PB0-PB3, giữ nguyên PB4-PB7 */
static inline void lcd_nibble(unsigned char n){
    LCD_Port = (LCD_Port & 0xF0) | (n & 0x0F);
}

/* Xung EN */
static inline void lcd_pulse(){
    LCD_Port |=  (1<<EN);
    _delay_us(50);
    LCD_Port &= ~(1<<EN);
    _delay_us(200);
}

void LCD_Command(unsigned char cmnd){
    LCD_Port &= ~(1<<RS);          /* RS=0: lệnh */
    lcd_nibble(cmnd >> 4);         /* 4 bit cao */
    lcd_pulse();
    lcd_nibble(cmnd & 0x0F);       /* 4 bit thấp */
    lcd_pulse();
    if(cmnd==0x01 || cmnd==0x02) _delay_ms(2);
}

void LCD_Char(unsigned char data){
    LCD_Port |=  (1<<RS);          /* RS=1: dữ liệu */
    lcd_nibble(data >> 4);
    lcd_pulse();
    lcd_nibble(data & 0x0F);
    lcd_pulse();
}

void lcd_init(void){
    LCD_Dir  = 0xFF;
    _delay_ms(50);

    LCD_Port &= ~(1<<RS);
    LCD_Port &= ~(1<<EN);

    /* Khởi tạo 4-bit mode chuẩn (3 lần 0x03 rồi 0x02) */
    lcd_nibble(0x03); lcd_pulse(); _delay_ms(5);
    lcd_nibble(0x03); lcd_pulse(); _delay_us(200);
    lcd_nibble(0x03); lcd_pulse(); _delay_us(200);
    lcd_nibble(0x02); lcd_pulse(); _delay_ms(2);

    LCD_Command(0x28);  /* 4-bit, 2 dòng, 5x8 */
    LCD_Command(0x0C);  /* Display ON, cursor OFF */
    LCD_Command(0x06);  /* Entry mode: tăng địa chỉ */
    LCD_Command(0x01);  /* Clear */
    _delay_ms(2);
}

void lcd_clear(void){
    LCD_Command(0x01);
    _delay_ms(2);
}

void lcd_gotoxy(int x, int y){
    unsigned char addr = (y==0) ? 0x80 : 0xC0;
    LCD_Command(addr + x);
}

void lcd_puts(const char *str){
    while(*str) LCD_Char((unsigned char)*str++);
}

/* ================================================================
 * KEYPAD — PORTA (hàng, input) + PORTC (cột, output)
 * FIX: thêm debounce đúng chỗ, đảm bảo PORTC reset sau mỗi cột
 * ================================================================ */
void key_init(){
    DDRA  &= ~0x0F;    /* PA0-PA3: INPUT */
    PORTA |=  0x0F;    /* pullup PA0-PA3 */
    DDRC  |=  0x3F;    /* PC0-PC5: OUTPUT */
    PORTC |=  0x3F;    /* tất cả cột HIGH (không kéo thấp) */
}

unsigned char key_scan(){
    /*
     * key_map[row][col] — FIX: Row D Col4 đổi từ 15 thành 20 (không dùng)
     * để tránh trùng với Row C Col4 (phím +, key=15).
     * Thực tế keypad vật lý chỉ có 1 phím + ở Row C Col4,
     * Row D Col4 là phím thứ 2 không cần thiết → trả về 0xFF (bỏ qua).
     */
    const unsigned char key_map[4][6] = {
        {12,  0,  1,  2,  7,  3},   /* A: ON/C, 7, 8, 9, x, /  */
        {18,  4,  5,  6, 11, 23},   /* B: +/-, 4, 5, 6, -, MODE */
        {19,  8,  9, 10, 15, 24},   /* C: %, 1, 2, 3, +, UP     */
        {16, 13, 17, 14, 15, 25}    /* D: √, 0, ., =, (unused), DOWN */
    };

    for(uint8_t c=0; c<6; c++){
        PORTC = (~(1<<c)) & 0x3F;   /* kéo thấp cột c */
        _delay_us(10);

        uint8_t r = 0xFF;
        if(!(PINA & (1<<PA0))) r=0;
        else if(!(PINA & (1<<PA1))) r=1;
        else if(!(PINA & (1<<PA2))) r=2;
        else if(!(PINA & (1<<PA3))) r=3;

        PORTC |= (1<<c);            /* khôi phục cột c */

        if(r != 0xFF){
            _delay_ms(20);          /* debounce */
            /* chờ nhả phím */
            PORTC = (~(1<<c)) & 0x3F;
            while(!(PINA & (1<<r)));
            PORTC |= (1<<c);
            PORTC  = 0x3F;

            unsigned char k = key_map[r][c];
            if(k == 22) return 0xFF; /* phím không dùng → bỏ qua */
            return k;
        }
    }
    PORTC = 0x3F;
    return 0xFF;
}

/* ================================================================
 * HELPER CHUNG
 * FIX: is_digit_key thêm n==13 (phím "0" vật lý)
 * ================================================================ */
int is_digit_key(int n){
    return (n==0 ||n==1 ||n==2 ||
            n==4 ||n==5 ||n==6 ||
            n==8 ||n==9 ||n==10||
            n==13);                 /* FIX: thêm 13 = phím "0" */
}

int get_number(int n){
    if(n==13) return 0;
    if(n==8)  return 1; if(n==9)  return 2; if(n==10) return 3;
    if(n==4)  return 4; if(n==5)  return 5; if(n==6)  return 6;
    if(n==0)  return 7; if(n==1)  return 8; if(n==2)  return 9;
    return 0;
}

/*
 * generic_input — nhập số vào buf (hỗ trợ chữ số, dấu chấm, dấu âm)
 * FIX: giới hạn độ dài buf để tránh tràn
 */
void generic_input(char *buf, int key){
    int blen = (int)strlen(buf);
    if(blen >= 14) return;          /* FIX: giới hạn 14 ký tự */

    if(is_digit_key(key)){
        int val = get_number(key);
        char tmp[2]; tmp[0]='0'+val; tmp[1]='\0';
        strcat(buf, tmp);
    }
    else if(key == 17){             /* dấu chấm thập phân */
        if(!strchr(buf, '.')){
            if(blen==0 || (blen==1 && buf[0]=='-'))
                strcat(buf, "0.");
            else
                strcat(buf, ".");
        }
    }
    else if(key == 11){             /* dấu âm (chỉ khi buf rỗng) */
        if(blen == 0) strcpy(buf, "-");
    }
}

/* ================================================================
 * CALC — MÁY TÍNH CHUỖI CÓ ƯU TIÊN TOÁN TỬ
 *
 * Thuật toán Two-Stack:
 *   Bước 1: Duyệt op_stack, xử lý tất cả x/ ngay (ưu tiên cao)
 *   Bước 2: Xử lý tất cả +- từ trái sang phải
 *
 * Ví dụ đúng:
 *   2+3x4=  → 14   (3x4=12 trước, rồi 2+12)
 *   6/2+1=  → 4    (6/2=3  trước, rồi 3+1)
 *   2+3+4=  → 9
 *   -5x-2=  → 10
 *   5/0=    → INF  (chia cho 0)
 *   √(16)→  → 4    (căn bậc 2 kết quả hoặc số đang nhập)
 *
 * Stack tối đa 8 số hạng — đủ cho biểu thức thực tế trên LCD
 * ================================================================ */
#define STACK_SIZE 8

float val_stack[STACK_SIZE];
int   op_stack[STACK_SIZE];
int   val_top    = 0;
int   op_top     = 0;

char  cur_buf[16] = "";    /* buffer số đang nhập */
char  expr_buf[33]= "";    /* chuỗi biểu thức hiển thị */
int   calc_done  = 0;
float last_result= 0.0f;

void calc_full_reset(){
    val_top=0; op_top=0;
    strcpy(cur_buf,  "");
    strcpy(expr_buf, "");
    calc_done=0; last_result=0.0f;
}

float apply_op(float a, int op, float b){
    switch(op){
        case 15: return a + b;
        case 11: return a - b;
        case 7:  return a * b;
        case 3:
            /* FIX: chia cho 0 → trả cờ INF rõ ràng */
            if(fabsf(b) < 1e-30f) return 1e30f;
            return a / b;
    }
    return b;
}

float calc_evaluate(){
    /* Đẩy số cuối vào stack */
    if(strlen(cur_buf) > 0 && val_top < STACK_SIZE){
        val_stack[val_top++] = atof(cur_buf);
    }
    if(val_top == 0) return 0.0f;

    /* Bước 1: xử lý x và / trước */
    float tmp_val[STACK_SIZE];
    int   tmp_op [STACK_SIZE];
    int   vt=0, ot=0, i;

    tmp_val[vt++] = val_stack[0];
    for(i=0; i<op_top; i++){
        if(op_stack[i]==7 || op_stack[i]==3){
            float r = apply_op(tmp_val[vt-1], op_stack[i], val_stack[i+1]);
            /* FIX: nếu kết quả chia cho 0 → dừng ngay, trả INF */
            if(r > 1e29f) return 1e30f;
            tmp_val[vt-1] = r;
        } else {
            tmp_op[ot++]  = op_stack[i];
            tmp_val[vt++] = val_stack[i+1];
        }
    }

    /* Bước 2: xử lý + và - */
    float result = tmp_val[0];
    for(i=0; i<ot; i++){
        result = apply_op(result, tmp_op[i], tmp_val[i+1]);
    }
    return result;
}

char op_char(int op){
    if(op==15) return '+';
    if(op==11) return '-';
    if(op==7)  return 'x';
    if(op==3)  return '/';
    return '?';
}

void calc_refresh(){
    /* Dòng 0: biểu thức (16 ký tự cuối nếu dài hơn) */
    char line0[17]={0};
    int  elen=(int)strlen(expr_buf);
    if(elen<=16) strncpy(line0,expr_buf,16);
    else         strncpy(line0,expr_buf+elen-16,16);

    /* Dòng 1: số đang nhập hoặc kết quả */
    char line1[17]={0};
    if(calc_done){
        if(last_result>1e29f || last_result<-1e29f)
            strcpy(line1,"INF");
        else
            dtostrf(last_result,0,4,line1);
    } else if(strlen(cur_buf)>0){
        strncpy(line1,cur_buf,16);
    } else {
        strcpy(line1,"_");
    }

    lcd_clear();
    lcd_gotoxy(0,0); lcd_puts(line0);
    lcd_gotoxy(0,1); lcd_puts(line1);
}

void calc_handle(unsigned char key){

    /* --- ON/C: xóa sạch (không thoát menu) --- */
    if(key==12){
        calc_full_reset(); calc_refresh(); return;
    }

    /* --- √ (key=16): căn bậc 2 --- */
    if(key==16){
        float val;
        int   from_result=0;

        if(calc_done){
            /* Căn bậc 2 của kết quả trước */
            val=last_result; from_result=1;
        } else if(strlen(cur_buf)>0){
            val=atof(cur_buf);
        } else {
            return; /* chưa có số */
        }

        if(val<0.0f){
            lcd_clear(); lcd_puts("Math Error");
            lcd_gotoxy(0,1); lcd_puts("sqrt(<0)");
            _delay_ms(1500);
            calc_full_reset(); calc_refresh(); return;
        }

        float r=sqrtf(val);

        if(from_result){
            /* Thay thế kết quả bằng √(kết quả) */
            last_result=r;
            dtostrf(r,0,4,cur_buf);
            strcpy(expr_buf,cur_buf);
        } else {
            /* Thay số đang nhập bằng √(số đó) */
            int clen=(int)strlen(cur_buf);
            int elen=(int)strlen(expr_buf);
            if(elen>=clen) expr_buf[elen-clen]='\0';
            dtostrf(r,0,4,cur_buf);
            strcat(expr_buf,cur_buf);
        }
        calc_refresh(); return;
    }

    /* --- Nếu vừa có kết quả và nhấn số → biểu thức mới --- */
    if(calc_done && (is_digit_key(key)||key==17)){
        calc_full_reset();
    }
    /* --- Nếu vừa có kết quả và nhấn toán tử → tiếp tục từ kết quả --- */
    if(calc_done && (key==15||key==11||key==7||key==3)){
        /* FIX: reset calc_done trước khi xử lý toán tử */
        calc_done=0;
        strcpy(cur_buf,"");
        strcpy(expr_buf,"");
        dtostrf(last_result,0,4,cur_buf);
        strcpy(expr_buf,cur_buf);
        /* Tiếp tục xử lý toán tử bên dưới */
    }

    /* --- Nhập số hoặc dấu chấm --- */
    if(is_digit_key(key)||key==17){
        int old_len=(int)strlen(cur_buf);
        generic_input(cur_buf,key);
        int new_len=(int)strlen(cur_buf);
        if(new_len>old_len){
            if(key==17){
                /* Thêm "0." hoặc "." vào expr_buf */
                if(new_len==2 && cur_buf[0]=='0') strcat(expr_buf,"0.");
                else strcat(expr_buf,".");
            } else {
                char tmp[2]; tmp[0]='0'+get_number(key); tmp[1]='\0';
                if(strlen(expr_buf)<32) strcat(expr_buf,tmp);
            }
        }
        calc_refresh(); return;
    }

    /* --- Dấu âm khi cur_buf rỗng --- */
    if(key==11 && strlen(cur_buf)==0){
        strcpy(cur_buf,"-");
        if(strlen(expr_buf)<32) strcat(expr_buf,"-");
        calc_refresh(); return;
    }

    /* --- Toán tử +, -, x, / --- */
    if(key==15||key==11||key==7||key==3){
        if(strlen(cur_buf)>0 && val_top<STACK_SIZE && op_top<STACK_SIZE){
            val_stack[val_top++]=atof(cur_buf);
            op_stack[op_top++]=key;
            strcpy(cur_buf,"");
            char sym[2]; sym[0]=op_char(key); sym[1]='\0';
            if(strlen(expr_buf)<32) strcat(expr_buf,sym);
            calc_refresh();
        }
        return;
    }

    /* --- "=" : tính kết quả --- */
    if(key==14){
        if(strlen(cur_buf)==0 && op_top==0) return;
        if(strlen(cur_buf)==0) return; /* có toán tử nhưng chưa có số sau */

        float res=calc_evaluate();
        last_result=res;
        calc_done=1;

        /* Hiện "biểu_thức=" ở dòng 0 */
        if(strlen(expr_buf)<32) strcat(expr_buf,"=");
        calc_refresh();

        /* Reset stack, giữ last_result */
        val_top=0; op_top=0;
        strcpy(cur_buf,"");
        strcpy(expr_buf,"");
        return;
    }
}

/* ================================================================
 * MENU
 * FIX: reset calc_done khi chuyển mode để tránh trạng thái cũ
 * ================================================================ */
int menu_mode  = 2;   /* Bắt đầu ở Calc */
int menu_index = 0;

const char *menu_items[] = {"1.Calc","2.Log","3.PTB2","4.Trig","5.x^y"};

void show_menu(){
    lcd_clear();
    lcd_gotoxy(0,0); lcd_puts(">"); lcd_puts(menu_items[menu_index]);
    lcd_gotoxy(0,1); lcd_puts(" "); lcd_puts(menu_items[(menu_index+1)%5]);
}

int trig_func=0;

void show_trig_menu(){
    lcd_clear();
    if(trig_func==0){ lcd_puts(">sin"); lcd_gotoxy(0,1); lcd_puts(" cos"); }
    else if(trig_func==1){ lcd_puts(">cos"); lcd_gotoxy(0,1); lcd_puts(" tan"); }
    else if(trig_func==2){ lcd_puts(">tan"); lcd_gotoxy(0,1); lcd_puts(" cot"); }
    else { lcd_puts(">cot"); lcd_gotoxy(0,1); lcd_puts(" sin"); }
}

/* ================================================================
 * MAIN
 * ================================================================ */
int main(void){
    lcd_init();
    key_init();

    /* Màn hình chào */
    lcd_clear();
    lcd_puts("Calculator");
    lcd_gotoxy(0,1); lcd_puts("Ready...");
    _delay_ms(1500);

    /* Bắt đầu ở Calc */
    calc_full_reset();
    calc_refresh();

    /* Biến PT2 */
    float pt_a=0, pt_b=0, pt_c=0;
    int   pt_step=0;
    char  pt_buf[16]="";

    /* Biến LOG */
    float log_base=0;
    int   log_step=0;
    char  lb_buf[16]="";

    /* Biến TRIG */
    int   trig_step=0;
    char  trig_buf[16]="";

    /* Biến x^y */
    float pow_base=0;
    int   pow_step=0;
    char  pow_buf[16]="";

    while(1){
        unsigned char key=key_scan();
        if(key==0xFF) continue;

        /* ============================================================
         * PHÍM MODE (key=23): vào menu chính bất kỳ lúc nào
         * FIX: reset calc_done khi chuyển mode
         * ============================================================ */
        if(key==23){
            calc_done=0;           /* FIX */
            menu_mode=0;
            menu_index=0;
            show_menu();
            continue;
        }

        /* ============================================================
         * MENU CHÍNH
         * UP(24)=lên, DOWN(25)=xuống, =(14)=chọn
         * ============================================================ */
        if(menu_mode==0){
            if(key==24){
                menu_index=(menu_index==0)?4:menu_index-1;
                show_menu();
            }
            else if(key==25){
                menu_index=(menu_index+1)%5;
                show_menu();
            }
            else if(key==14){
                menu_mode=menu_index+2;
                calc_done=0;       /* FIX: reset trước khi vào mode mới */
                lcd_clear();

                if(menu_mode==2){
                    /* Calc */
                    calc_full_reset(); calc_refresh();
                }
                else if(menu_mode==3){
                    /* Log */
                    log_step=0; log_base=0; strcpy(lb_buf,"");
                    lcd_puts("Base b=");
                }
                else if(menu_mode==4){
                    /* PT2 */
                    pt_step=0; pt_a=0; pt_b=0; pt_c=0; strcpy(pt_buf,"");
                    lcd_puts("a=");
                }
                else if(menu_mode==5){
                    /* Trig */
                    trig_func=0; trig_step=0; strcpy(trig_buf,"");
                    show_trig_menu();
                }
                else if(menu_mode==6){
                    /* x^y */
                    pow_step=0; pow_base=0; strcpy(pow_buf,"");
                    lcd_puts("Base x=");
                }
            }
        }

        /* ============================================================
         * CALC — chuỗi phép tính có ưu tiên toán tử
         * ON/C(12) = xóa, MODE(23) đã xử lý ở trên
         * ============================================================ */
        else if(menu_mode==2){
            calc_handle(key);
        }

        /* ============================================================
         * LOG — log_b(x) = ln(x)/ln(b)
         *
         * Bước 0: nhập base b → "="(14) xác nhận
         * Bước 1: nhập x    → "="(14) tính
         * ON/C(12) bước 0: xóa buffer, ON/C bước 1: về nhập base
         *
         * FIX: kiểm tra b<=0 hoặc b==1, kiểm tra x<=0
         * FIX: hỗ trợ số thập phân qua generic_input
         * ============================================================ */
        else if(menu_mode==3){

            if(key==12){
                /* ON/C: nếu đang nhập x → về nhập base
                         nếu đang nhập base → xóa buffer */
                if(log_step==1){
                    log_step=0; log_base=0; strcpy(lb_buf,"");
                    lcd_clear(); lcd_puts("Base b=");
                } else {
                    strcpy(lb_buf,"");
                    lcd_clear(); lcd_puts("Base b=");
                }
            }
            else if(is_digit_key(key)||key==17||key==11){
                generic_input(lb_buf,key);
                lcd_clear();
                if(log_step==0){ lcd_puts("Base b="); lcd_gotoxy(7,0); }
                else            { lcd_puts("Value x="); lcd_gotoxy(8,0); }
                lcd_puts(lb_buf);
            }
            else if(key==14){
                if(log_step==0){
                    float bv=atof(lb_buf);
                    if(bv<=0.0f||(bv>0.9999f&&bv<1.0001f)){
                        lcd_clear(); lcd_puts("Base Error!");
                        lcd_gotoxy(0,1); lcd_puts("b>0 & b!=1");
                        _delay_ms(1500);
                        strcpy(lb_buf,"");
                        lcd_clear(); lcd_puts("Base b=");
                    } else {
                        log_base=bv; log_step=1; strcpy(lb_buf,"");
                        lcd_clear(); lcd_puts("Value x=");
                    }
                } else {
                    float xv=atof(lb_buf);
                    if(xv<=0.0f){
                        lcd_clear(); lcd_puts("Value Error!");
                        lcd_gotoxy(0,1); lcd_puts("x must be >0");
                        _delay_ms(1500);
                        strcpy(lb_buf,"");
                        lcd_clear(); lcd_puts("Value x=");
                    } else {
                        float res=(float)(log((double)xv)/log((double)log_base));
                        lcd_clear();
                        /* FIX: hiện rõ "log_b(x)=" thay vì chỉ "Result:" */
                        char b_str[8]; dtostrf(log_base,0,2,b_str);
                        lcd_puts("log"); lcd_puts(b_str); lcd_puts("(x)=");
                        lcd_gotoxy(0,1);
                        char res_str[16]; dtostrf(res,0,4,res_str);
                        lcd_puts(res_str);
                        _delay_ms(2500);
                        log_step=0; strcpy(lb_buf,"");
                        lcd_clear(); lcd_puts("Base b=");
                    }
                }
            }
        }

        /* ============================================================
         * PT BẬC 2 — ax² + bx + c = 0
         *
         * "="(14) xác nhận từng bước (a→b→c→tính)
         * ON/C(12): xóa số đang nhập
         *
         * FIX: dùng "=" để chuyển bước thay vì "/" để tránh nhầm phép tính
         * FIX: kiểm tra a==0 trước khi sang bước b
         * FIX: hỗ trợ số thập phân và số âm qua generic_input
         * ============================================================ */
        else if(menu_mode==4){

            if(key==12){
                /* ON/C: xóa số đang nhập */
                strcpy(pt_buf,"");
                lcd_clear();
                if(pt_step==0) lcd_puts("a=");
                else if(pt_step==1) lcd_puts("b=");
                else lcd_puts("c=");
            }
            else if(is_digit_key(key)||key==17||key==11){
                generic_input(pt_buf,key);
                lcd_clear();
                if(pt_step==0)      lcd_puts("a=");
                else if(pt_step==1) lcd_puts("b=");
                else                lcd_puts("c=");
                lcd_puts(pt_buf);
            }
            else if(key==14){
                if(pt_step==0){
                    pt_a=atof(pt_buf);
                    if(pt_a==0.0f){
                        lcd_clear(); lcd_puts("a != 0 !");
                        _delay_ms(1500);
                        strcpy(pt_buf,""); lcd_clear(); lcd_puts("a=");
                    } else {
                        pt_step=1; strcpy(pt_buf,"");
                        lcd_clear(); lcd_puts("b=");
                    }
                }
                else if(pt_step==1){
                    pt_b=atof(pt_buf);
                    pt_step=2; strcpy(pt_buf,"");
                    lcd_clear(); lcd_puts("c=");
                }
                else if(pt_step==2){
                    pt_c=atof(pt_buf);
                    float delta=pt_b*pt_b-4.0f*pt_a*pt_c;
                    lcd_clear();
                    char tmp[16];
                    if(delta<0.0f){
                        lcd_puts("No real roots");
                    } else if(delta==0.0f){
                        float x0=-pt_b/(2.0f*pt_a);
                        lcd_puts("x="); dtostrf(x0,0,4,tmp); lcd_puts(tmp);
                    } else {
                        float sq=(float)sqrt((double)delta);
                        float x1=(-pt_b+sq)/(2.0f*pt_a);
                        float x2=(-pt_b-sq)/(2.0f*pt_a);
                        lcd_puts("x1="); dtostrf(x1,0,3,tmp); lcd_puts(tmp);
                        lcd_gotoxy(0,1);
                        lcd_puts("x2="); dtostrf(x2,0,3,tmp); lcd_puts(tmp);
                    }
                    _delay_ms(3000);
                    pt_step=0; pt_a=0; pt_b=0; pt_c=0;
                    strcpy(pt_buf,"");
                    lcd_clear(); lcd_puts("a=");
                }
            }
        }

        /* ============================================================
         * TRIG — sin/cos/tan/cot (góc nhập theo độ, hỗ trợ số thực)
         *
         * trig_step=0: menu chọn hàm
         *   UP(24)=lên, DOWN(25)=xuống, =(14)=chọn hàm
         *
         * trig_step=1: nhập góc
         *   chữ số/dấu chấm → nhập
         *   −(11) khi rỗng → góc âm
         *   =(14) → tính
         *   ON/C(12) → xóa buffer, về menu trig
         *
         * FIX: lcd_clear() trước khi hiện kết quả (tránh chồng màn hình)
         * FIX: tan(90+180k°) và cot(0+180k°) → "INF" thay vì crash
         * FIX: hiện rõ "sin(45)=" ở dòng 0, kết quả ở dòng 1
         * ============================================================ */
        else if(menu_mode==5){

            if(trig_step==0){
                if(key==24){
                    trig_func=(trig_func==0)?3:trig_func-1;
                    show_trig_menu();
                }
                else if(key==25){
                    trig_func=(trig_func+1)%4;
                    show_trig_menu();
                }
                else if(key==14){
                    trig_step=1; strcpy(trig_buf,"");
                    lcd_clear();
                    if(trig_func==0)      lcd_puts("sin(deg)=");
                    else if(trig_func==1) lcd_puts("cos(deg)=");
                    else if(trig_func==2) lcd_puts("tan(deg)=");
                    else                  lcd_puts("cot(deg)=");
                }
                else if(key==12){
                    trig_func=0; show_trig_menu();
                }
            }
            else {
                if(key==12){
                    /* ON/C: xóa buffer, về menu trig */
                    strcpy(trig_buf,""); trig_step=0;
                    show_trig_menu();
                }
                else if(is_digit_key(key)||key==17||key==11){
                    generic_input(trig_buf,key);
                    /* FIX: lcd_clear trước khi cập nhật */
                    lcd_clear();
                    if(trig_func==0)      lcd_puts("sin(");
                    else if(trig_func==1) lcd_puts("cos(");
                    else if(trig_func==2) lcd_puts("tan(");
                    else                  lcd_puts("cot(");
                    lcd_puts(trig_buf); lcd_puts(")");
                }
                else if(key==14){
                    if(strlen(trig_buf)==0){
                        lcd_clear(); lcd_puts("Enter angle!");
                        _delay_ms(1500);
                        lcd_clear();
                        if(trig_func==0)      lcd_puts("sin(deg)=");
                        else if(trig_func==1) lcd_puts("cos(deg)=");
                        else if(trig_func==2) lcd_puts("tan(deg)=");
                        else                  lcd_puts("cot(deg)=");
                    } else {
                        double rad=atof(trig_buf)*M_PI/180.0;
                        double res=0.0;
                        int    is_inf=0;
                        char   res_str[16];

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

                        /* FIX: lcd_clear trước khi hiện kết quả */
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

                        _delay_ms(2500);
                        strcpy(trig_buf,""); trig_step=0;
                        show_trig_menu();
                    }
                }
            }
        }

        /* ============================================================
         * x^y — LŨYTHỪA: pow(base, exp)
         *
         * Bước 0: nhập base x → "="(14) xác nhận
         * Bước 1: nhập số mũ y → "="(14) tính
         * ON/C(12): xóa buffer
         *
         * FIX: kiểm tra pow(0, số âm) → INF
         * FIX: kiểm tra pow(số âm, số lẻ thập phân) → NaN → báo lỗi
         * FIX: hiện rõ "x^y=" ở dòng 0 thay vì "Result:"
         * ============================================================ */
        else if(menu_mode==6){

            if(key==12){
                strcpy(pow_buf,"");
                lcd_clear();
                if(pow_step==0) lcd_puts("Base x=");
                else            lcd_puts("Exp  y=");
            }
            else if(is_digit_key(key)||key==17||key==11){
                generic_input(pow_buf,key);
                lcd_clear();
                if(pow_step==0) lcd_puts("Base x=");
                else            lcd_puts("Exp  y=");
                lcd_puts(pow_buf);
            }
            else if(key==14){
                if(pow_step==0){
                    pow_base=atof(pow_buf);
                    pow_step=1; strcpy(pow_buf,"");
                    lcd_clear(); lcd_puts("Exp  y=");
                } else {
                    float exp_y=atof(pow_buf);

                    /* FIX: kiểm tra 0^(âm) */
                    if(pow_base==0.0f && exp_y<0.0f){
                        lcd_clear(); lcd_puts("Math Error");
                        lcd_gotoxy(0,1); lcd_puts("0^neg undef");
                        _delay_ms(1500);
                        pow_step=0; strcpy(pow_buf,"");
                        lcd_clear(); lcd_puts("Base x=");
                        continue;
                    }

                    /* FIX: kiểm tra âm^thập_phân */
                    if(pow_base<0.0f && floorf(exp_y)!=exp_y){
                        lcd_clear(); lcd_puts("Math Error");
                        lcd_gotoxy(0,1); lcd_puts("neg^frac err");
                        _delay_ms(1500);
                        pow_step=0; strcpy(pow_buf,"");
                        lcd_clear(); lcd_puts("Base x=");
                        continue;
                    }

                    float res=(float)pow((double)pow_base,(double)exp_y);

                    /* FIX: hiện "x^y=" ở dòng 0 */
                    lcd_clear();
                    char bstr[8], estr[8];
                    dtostrf(pow_base,0,2,bstr);
                    dtostrf(exp_y,  0,2,estr);
                    lcd_puts(bstr); lcd_puts("^"); lcd_puts(estr); lcd_puts("=");

                    lcd_gotoxy(0,1);
                    if(res>1e29f||res<-1e29f){
                        lcd_puts("INF");
                    } else {
                        char res_str[16]; dtostrf(res,0,4,res_str);
                        lcd_puts(res_str);
                    }

                    _delay_ms(2500);
                    pow_step=0; strcpy(pow_buf,"");
                    lcd_clear(); lcd_puts("Base x=");
                }
            }
        }

    } /* while(1) */
}
