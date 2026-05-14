#define F_CPU 8000000UL
#include <avr/io.h>
#include <util/delay.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
 
#ifndef M_PI
#define M_PI 3.14159265358979323846 // Định nghĩa hằng số Pi nếu chưa có
#endif
 
/* =================================================================
 *
PHẦN CỨNG: LCD 16x2 & Keypad 4x6 
 *
================================================================= */
// Khai báo các chân giao tiếp LCD trên PORTB
#define LCD_Port PORTB
#define LCD_Dir  DDRB
#define RS       PB5
#define EN       PB4
 
// Gửi 4 bit (nibble) dữ liệu hoặc lệnh ra LCD
static inline void lcd_nibble(unsigned char n){
   LCD_Port = (LCD_Port & 0xF0) | (n & 0x0F);
}
 
// Tạo xung kích hoạt (Enable pulse) để LCD đọc dữ liệu
static inline void lcd_pulse(){
   LCD_Port |=  (1<<EN);
   _delay_us(50);
   LCD_Port &= ~(1<<EN);
   _delay_us(200);
}
 
// Gửi mã lệnh (Command) cho LCD
void LCD_Command(unsigned char cmnd){
   LCD_Port &= ~(1<<RS); // RS = 0 để gửi lệnh
   lcd_nibble(cmnd >> 4); // Gửi 4 bit cao
   lcd_pulse();
   lcd_nibble(cmnd & 0x0F); // Gửi 4 bit thấp
   lcd_pulse();
   if(cmnd==0x01 || cmnd==0x02) _delay_ms(2); // Trễ lâu hơn cho lệnh xóa/về đầu
}
 
// Gửi một ký tự (Data) hiển thị lên LCD
void LCD_Char(unsigned char data){
   LCD_Port |= (1<<RS); // RS = 1 để gửi dữ liệu
   lcd_nibble(data >> 4);
   lcd_pulse();
   lcd_nibble(data & 0x0F);
   lcd_pulse();
}
 
// Khởi tạo LCD chế độ 4-bit
void lcd_init(void){
   LCD_Dir  = 0xFF; // Cấu hình PORTB là output
   _delay_ms(50);
   LCD_Port &= ~(1<<RS);
   LCD_Port &= ~(1<<EN);
 
   // Trình tự khởi tạo chuẩn cho LCD 16x2 (chế độ 4-bit)
   lcd_nibble(0x03); lcd_pulse(); _delay_ms(5);
   lcd_nibble(0x03); lcd_pulse(); _delay_us(200);
   lcd_nibble(0x03); lcd_pulse(); _delay_us(200);
   lcd_nibble(0x02); lcd_pulse(); _delay_ms(2);
 
   LCD_Command(0x28); // 4-bit mode, 2 dòng, font 5x8
   LCD_Command(0x0C); // Bật hiển thị, tắt con trỏ
   LCD_Command(0x06); // Tự động tăng con trỏ
   LCD_Command(0x01); // Xóa màn hình
   _delay_ms(2);
}
 
// Xóa màn hình LCD
void lcd_clear(void){
   LCD_Command(0x01);
   _delay_ms(2);
}
 
// Di chuyển con trỏ LCD tới tọa độ (x, y)
void lcd_gotoxy(int x, int y){
   unsigned char addr = (y==0) ? 0x80 : 0xC0; // 0x80 là dòng 1, 0xC0 là dòng 2
   LCD_Command(addr + x);
}
 
// In chuỗi ký tự ra LCD
void lcd_puts(const char *str){
   while(*str) LCD_Char((unsigned char)*str++);
}
 
/* =================================================================
 *
KEYPAD 4x6 — PORTA (hàng, input) + PORTC (cột, output)
 *
================================================================= */
// Định nghĩa mã cho các phím chức năng đặc biệt
#define KEY_ON_C   250
#define KEY_MODE   251
#define KEY_SHIFT  252
#define KEY_SQRT   253
#define KEY_DOWN   254
 
// Khởi tạo cổng kết nối Keypad
void key_init(){
   DDRA  &= ~0x0F; // PA0-PA3 (Hàng) là Input
   PORTA |=  0x0F; // Bật điện trở kéo lên (Pull-up) cho Hàng
   DDRC  |=  0x3F; // PC0-PC5 (Cột) là Output
   PORTC |=  0x3F; // Mặc định Cột ở mức CAO
}
 
// Quét ma trận phím để xem phím nào đang được nhấn
unsigned char key_scan(){
   // Bảng ánh xạ ký tự tương ứng với phím 4x6
   const unsigned char key_map[4][6] = {
      {KEY_ON_C, '7', '8', '9', 'x', '/'},       
      {'(',      '4', '5', '6', '-', KEY_MODE},  
      {')',      '1', '2', '3', '+', KEY_SHIFT}, 
      {KEY_SQRT, '0', '.', '=', '+', KEY_DOWN}   
   };
 
   for(uint8_t c=0; c<6; c++){
      PORTC = (~(1<<c)) & 0x3F; // Kéo lần lượt từng cột xuống LOW
      _delay_us(10);
 
      uint8_t r = 0xFF; // Khởi tạo cờ kiểm tra hàng
      if(!(PINA & (1<<PA0))) r=0;
      else if(!(PINA & (1<<PA1))) r=1;
      else if(!(PINA & (1<<PA2))) r=2;
      else if(!(PINA & (1<<PA3))) r=3;
 
      PORTC |= (1<<c); // Trả cột về mức HIGH
 
      // Nếu có phím được nhấn (r != 0xFF)
      if(r != 0xFF){
          _delay_ms(20);  // Chống dội phím (Debounce)
          PORTC = (~(1<<c)) & 0x3F;
          while(!(PINA & (1<<r))); // Chờ người dùng nhả phím
          PORTC = 0x3F;
          return key_map[r][c]; // Trả về mã phím
      }
   }
   PORTC = 0x3F;
   return 0xFF; // Không có phím nào được nhấn
}
 
/* =================================================================
 *
HELPER CHUNG & TỐI ƯU HIỂN THỊ
 *
================================================================= */
#define SQRT_CHAR ((char)0xE8) // Mã ký tự căn bậc 2 (tùy ROM LCD)
 
// Hàm tính lũy thừa, ưu tiên phép tính số nguyên chính xác nếu có thể
float exact_pow(float base, float exp) {
   if (floorf(base) == base && floorf(exp) == exp && exp >= 0.0f) {
       long res = 1;
       long b = (long)base;
       int e = (int)exp;
       for (int i = 0; i < e; i++) res *= b;
       return (float)res;
   }
   return (float)pow((double)base, (double)exp);
}
 
// Định dạng số float thành chuỗi hiển thị gọn gàng (cắt số 0 thừa)
void format_display_float(float val, char *buf) {
   dtostrf(val, 0, 4, buf); // Chuyển float sang chuỗi, 4 chữ số thập phân
   int len = strlen(buf);
   if (strchr(buf, '.')) { // Cắt bỏ các số 0 ở cuối
       while (len > 0 && buf[len - 1] == '0') { buf[len - 1] = '\0'; len--; }
       if (len > 0 && buf[len - 1] == '.') buf[len - 1] = '\0'; // Xóa luôn dấu chấm nếu k có thập phân
   }
}
 
// Xử lý nhập liệu chung (số, dấu chấm, dấu trừ)
void generic_input(char *buf, unsigned char key){
   int blen = (int)strlen(buf);
   if(blen >= 14) return; // Giới hạn độ dài chuỗi nhập
   if(key >= '0' && key <= '9'){
       char tmp[2] = {key, '\0'};
       strcat(buf, tmp); // Nối số vào chuỗi
   }
   else if(key == '.'){
       if(!strchr(buf, '.')){ // Đảm bảo chỉ có 1 dấu chấm
            if(blen==0 || (blen==1 && buf[0]=='-')) strcat(buf, "0."); // Tự động thêm '0' nếu ấn '.' đầu tiên
            else strcat(buf, ".");
       }
   }
   else if(key == '-'){
       if(blen == 0) strcpy(buf, "-"); // Chỉ cho phép dấu âm ở đầu
   }
}
 
/*
=================================================================
 *
CALC — SHUNTING-YARD PARSER PRO (Thuật toán chuyển đổi và tính toán)
 *
================================================================= */
#define EXPR_MAX  64 // Độ dài tối đa biểu thức
#define SY_STACK  32 // Độ lớn tối đa của stack
 
// Định nghĩa mã lỗi
#define VAL_INF   1e29f // Vô cực
#define VAL_ERR   1e30f // Lỗi toán học
 
int menu_mode  = 1; // 1: Calc, 2: Giải PT bậc 2
int menu_index = 0;
int is_shift   = 0; // Cờ phím Shift
 
// Hàm xác định mức độ ưu tiên của toán tử
int sy_prec(char op){
   if(op=='p' || op=='L') return 3; // Mũ (p) và Logarit (L) ưu tiên cao nhất
   if(op=='x' || op=='/') return 2; // Nhân, chia
   if(op=='+' || op=='-') return 1; // Cộng, trừ
   return 0;
}
 
// Tính toán phép toán 2 ngôi
float sy_apply_binary(float a, char op, float b){
   if (op == '+') return a + b;
   if (op == '-') return a - b;
   if (op == 'x') return a * b;
   if (op == '/') { 
       if(fabsf(b) < 1e-30f) return (a >= 0 ? VAL_INF : -VAL_INF); // Lỗi chia cho 0
       return a / b; 
   }
   if (op == 'p') {
       if (a == 0 && b < 0) return VAL_INF; 
       if (a < 0 && floorf(b) != b) return VAL_ERR; // Căn bậc chẵn của số âm
       return exact_pow(a, b);
   }
   if (op == 'L') {
       if (a <= 0 || a == 1.0f || b <= 0) return VAL_ERR; // Điều kiện loga(b): a>0, a!=1, b>0
       return log10(b) / log10(a);
   }
   return b;
}
 
// Tính toán hàm toán học 1 ngôi (lượng giác, căn bậc 2)
float sy_apply_unary(char op, float a) {
   if (op == 's') return sin(a * M_PI / 180.0); // Tính theo độ (Degree)
   if (op == 'c') return cos(a * M_PI / 180.0);
   if (op == 't') {
       if (fabs(cos(a * M_PI / 180.0)) < 1e-6) return VAL_INF; // Điểm gián đoạn của tan
       return tan(a * M_PI / 180.0);
   }
   if (op == 'g') { // cotang
       if (fabs(sin(a * M_PI / 180.0)) < 1e-6) return VAL_INF;
       return 1.0 / tan(a * M_PI / 180.0);
   }
   if (op == 'r') {
       if (a < 0) return VAL_ERR; // Căn bậc 2 số âm
       return sqrt(a);
   }
   return a;
}
 
// Hàm phân tích và tính toán giá trị của biểu thức chuỗi (Thuật toán Shunting-yard)
float sy_evaluate(const char *expr){
   float val_stk[SY_STACK]; // Stack chứa giá trị (toán hạng)
   char  op_stk[SY_STACK];  // Stack chứa toán tử
   int   vt=0, ot=0, i=0;   // Chỉ số đỉnh của các stack
   int   len_e=(int)strlen(expr);
 
   while(i <= len_e){
      char c = expr[i];
      if(c==' '){ i++; continue; } // Bỏ qua khoảng trắng
 
      // Xử lý dấu phần trăm
      if(c=='%'){
          if(vt<1) return VAL_ERR;
          val_stk[vt-1] = val_stk[vt-1] / 100.0f;
          i++; continue;
      }
 
      // Xử lý ngoặc đóng hoặc kết thúc chuỗi
      if(c==')' || c=='\0'){
          while(ot>0 && op_stk[ot-1]!='('){ // Lấy toán tử ra tính đến khi gặp ngoặc mở
               char op = op_stk[--ot];
               if(vt<2) return VAL_ERR;
               float b = val_stk[--vt];
               float a = val_stk[--vt];
               val_stk[vt++] = sy_apply_binary(a, op, b); // Tính và đẩy kết quả lại vào stack
          }
          if(c==')' && ot>0 && op_stk[ot-1]=='(') {
               ot--; // Loại bỏ dấu '(' khỏi stack
               // Kích hoạt tính hàm 1 ngôi đằng trước ngoặc (nếu có: sin, cos...)
               if (ot>0 && (op_stk[ot-1]=='s' || op_stk[ot-1]=='c' || op_stk[ot-1]=='t' || op_stk[ot-1]=='g' || op_stk[ot-1]=='r')) {
                   char op = op_stk[--ot];
                   if(vt<1) return VAL_ERR;
                   val_stk[vt-1] = sy_apply_unary(op, val_stk[vt-1]);
               }
          }
          if(c=='\0') break; // Nếu là cuối chuỗi thì dừng
          i++; continue;
      }
 
      // Đẩy ngoặc mở hoặc toán tử hàm 1 ngôi vào stack toán tử
      if(c=='(' || c=='s' || c=='c' || c=='t' || c=='g' || c=='r'){
          if(ot<SY_STACK) op_stk[ot++]=c;
          i++; continue;
      }
 
      // Lấy số (và dấu âm ở đầu/sau ngoặc/toán tử)
      if((c>='0' && c<='9') ||
         (c=='-' && (i==0 || expr[i-1]=='(' || expr[i-1]=='+' ||
                     expr[i-1]=='-' || expr[i-1]=='x' || expr[i-1]=='/' || expr[i-1]=='p' || expr[i-1]=='L'))){
          float sign=1.0f;
          if(c=='-'){ sign=-1.0f; i++; c=expr[i]; }
          float num=0.0f;
          while(i<=len_e && expr[i]>='0' && expr[i]<='9'){ num=num*10.0f+(float)(expr[i]-'0'); i++; }
          if(i<=len_e && expr[i]=='.'){
               i++; float frac=0.1f;
               while(i<=len_e && expr[i]>='0' && expr[i]<='9'){
                   num+=(float)(expr[i]-'0')*frac; frac*=0.1f; i++;
               }
            }
          if(vt<SY_STACK) val_stk[vt++]=sign*num; // Đẩy toán hạng vào stack giá trị
          continue;
      }
 
      // Xử lý các toán tử 2 ngôi
      if(c=='+' || c=='-' || c=='x' || c=='/' || c=='p' || c=='L'){
          // Áp dụng phép toán đối với các toán tử có độ ưu tiên cao hơn hoặc bằng đang nằm trong stack
          while(ot>0 && op_stk[ot-1]!='(' && sy_prec(op_stk[ot-1])>=sy_prec(c)){
               char op = op_stk[--ot];
               if(vt<2) return VAL_ERR;
               float b = val_stk[--vt];
               float a = val_stk[--vt];
               val_stk[vt++] = sy_apply_binary(a, op, b);
          }
          if(ot<SY_STACK) op_stk[ot++]=c; // Đẩy toán tử hiện tại vào
          i++; continue;
      }
      i++; // Bỏ qua ký tự không hợp lệ
   }
 
   // Tính nốt phần còn lại trong stack
   while(ot > 0) {
       char op = op_stk[--ot];
       if (op == '(') continue;
       if (op == 's' || op == 'c' || op == 't' || op == 'g' || op == 'r') {
            if (vt < 1) return VAL_ERR;
            val_stk[vt-1] = sy_apply_unary(op, val_stk[vt-1]);
       } else {
            if (vt < 2) return VAL_ERR;
            float b = val_stk[--vt];
            float a = val_stk[--vt];
            val_stk[vt++] = sy_apply_binary(a, op, b);
       }
   }
 
   // Kết quả cuối cùng
   if(vt==1) return val_stk[0];
   return VAL_ERR;
}
 
char expr_buf[EXPR_MAX]  = ""; // Bộ đệm chứa biểu thức người dùng gõ
int  calc_done   = 0;          // Cờ báo hiệu đã bấm '='
float last_result= 0.0f;       // Biến lưu trữ kết quả lần tính trước (Ans)
int  paren_depth = 0;          // Bộ đếm theo dõi số lượng ngoặc mở chưa đóng
 
// Reset hoàn toàn trạng thái máy tính
void calc_full_reset(){
   strcpy(expr_buf, "");
   calc_done   = 0;
   last_result = 0.0f;
   paren_depth = 0;
   is_shift    = 0;
}
 
// Dịch các mã toán tử nội bộ thành chuỗi có thể đọc để hiện trên LCD
void build_display_string(char *disp) {
   disp[0] = '\0';
   int len = strlen(expr_buf);
   for(int i=0; i<len; i++) {
       char c = expr_buf[i];
       if(c == 's') strcat(disp, "sin");
       else if(c == 'c') strcat(disp, "cos");
       else if(c == 't') strcat(disp, "tan");
       else if(c == 'g') strcat(disp, "cot");
       else if(c == 'r') {
            char sq[2] = {SQRT_CHAR, '\0'}; // Căn bậc 2
            strcat(disp, sq);
       }
       else if(c == 'p') strcat(disp, "^"); // Mũ
       else if(c == 'L') strcat(disp, "L"); // In chữ L gọn gàng (Logarit)
       else {
           // Copy trực tiếp các ký tự bình thường (số, +, -, x...)
           int dl = strlen(disp);
           disp[dl] = c; disp[dl+1] = '\0';
       }
   }
}
 
// Cập nhật nội dung trên màn hình LCD (chế độ Calculator)
void calc_refresh(){
   char disp[128]; 
   build_display_string(disp); // Lấy chuỗi biểu thức để hiển thị
   int  elen=(int)strlen(disp);
   char line0[17]={0};
   
   // Cuộn màn hình để luôn hiển thị phần cuối của biểu thức dài
   if(elen<=16) strcpy(line0, disp);
   else         strncpy(line0, disp+elen-16, 16);
 
   lcd_clear();
   lcd_gotoxy(0,0); lcd_puts(line0);
 
   // Gợi ý đóng ngoặc ở dòng thứ 2
   if(!calc_done && paren_depth>0){
      char hint[17]={0};
      for(int h=0; h<paren_depth && h<8; h++) hint[h]='(';
      strcat(hint," open");
      lcd_gotoxy(0,1); lcd_puts(hint);
   }
 
   // Hiển thị chữ S nhỏ khi đang bật phím Shift
   if (is_shift) {
       lcd_gotoxy(15, 1); lcd_puts("S");
   }
}
 
// Nhập một hàm (sin, cos, tan...) vào biểu thức
void insert_function(char f) {
   int elen = strlen(expr_buf);
   if(elen < EXPR_MAX - 3) {
       if(elen > 0) {
            char last = expr_buf[elen-1];
            // Tự động thêm dấu 'x' nếu trước đó là số hoặc ngoặc đóng (VD: 2sin -> 2*sin)
            if((last >= '0' && last <= '9') || last == '.' || last == ')' || last == '%') {
                strcat(expr_buf, "x");
            }
       }
       char tmp[3] = {f, '(', '\0'}; // Cú pháp: mã_hàm + ngoặc mở
       strcat(expr_buf, tmp);
       paren_depth++;
       calc_refresh();
   }
}
 
// Nhập một toán tử (+, -, x, /, ^)
void insert_operator(char op) {
   int elen = strlen(expr_buf);
   char last = elen ? expr_buf[elen-1] : '\0';
   
   // Tối ưu UX: Cho phép nhập số âm ở đầu hoặc ngay sau các dấu (, hàm, hoặc bất kỳ toán tử nào
   if(elen == 0 || last == '(' || last == 's' || last == 'c' || last == 't' || last == 'g' || last == 'r' || 
      last == '+' || last == '-' || last == 'x' || last == '/' || last == 'p' || last == 'L') {
       if(op == '-') {
            if (last != '-') { // Chống nhập 2 dấu trừ liên tiếp
                if(strlen(expr_buf) < EXPR_MAX - 2) strcat(expr_buf, "-");
                calc_refresh();
            }
       }
       return;
   }
   // Thêm toán tử bình thường
   if((last>='0' && last<='9') || last=='.' || last==')' || last=='%'){
       char sym[2] = {op, '\0'};
       if(strlen(expr_buf) < EXPR_MAX - 2) strcat(expr_buf, sym);
       calc_refresh();
   }
}
 
// Hàm điều phối xử lý theo phím bấm trong chức năng Máy tính (Calculator Mode)
void calc_handle(unsigned char key){
   // Nếu nhấn phím SHIFT
   if (key == KEY_SHIFT) {
       if (is_shift) {
           // Tự nối kết quả cũ (Ans) nếu bấm Shift 2 lần ngay sau khi tính
           if (calc_done) {
               char tmp[16]; format_display_float(last_result, tmp);
               calc_full_reset(); strcat(expr_buf, tmp);
           }
           insert_operator('p'); // Thêm phép mũ ^
           is_shift = 0;
       } else {
           is_shift = 1; // Bật cờ Shift
       }
       calc_refresh();
       return;
   }
 
   // Xử lý khi cờ Shift đang bật
   if (is_shift) {
       is_shift = 0;
       // Bắt tính năng Ans nếu gọi hàm bằng Shift ngay sau khi ấn dấu '='
       if (key == '1' || key == '2' || key == '3' || key == '4' || key == '5' || key == '6') {
            if (calc_done) {
                char tmp[16]; format_display_float(last_result, tmp);
                calc_full_reset(); strcat(expr_buf, tmp);
            }
       }
       
       // Gán phím số cho các hàm lượng giác/logarit thông qua Shift
       if (key == '1') insert_function('s'); // Shift + 1 = sin(
       else if (key == '2') insert_function('c'); // Shift + 2 = cos(
       else if (key == '3') insert_function('t'); // Shift + 3 = tan(
       else if (key == '4') insert_function('g'); // Shift + 4 = cot(
       else if (key == '5') insert_operator('L'); // L (Toán tử Logarit)
       else if (key == '6') { // Shift + 6 = Thêm %
           int elen = strlen(expr_buf);
           char last = elen ? expr_buf[elen-1] : '\0';
           if ((last >= '0' && last <= '9') || last == ')' || last == '.') {
               if(strlen(expr_buf) < EXPR_MAX - 2) strcat(expr_buf, "%");
           }
       }
       calc_refresh();
       return;
   }
 
   // Nút ON/C: Bấm lần 1 để xóa 1 ký tự cuối (Backspace), nhấn sau khi bằng sẽ Clear (AC)
   if (key == KEY_ON_C) {
       int elen = (int)strlen(expr_buf);
       if(calc_done || elen == 0){
            calc_full_reset(); // Trở về 0 nếu đang rỗng hoặc vừa tính xong
            lcd_clear(); lcd_gotoxy(0,0); lcd_puts("0");
            return;
       }
       // Logic Backspace (Xóa lùi)
       char removed = expr_buf[elen-1];
       expr_buf[elen-1] = '\0';
       // Quản lý ngoặc
       if(removed == '(') {
           paren_depth--;
           int nlen = strlen(expr_buf);
           if (nlen > 0) {
               char last = expr_buf[nlen-1];
               // Nếu lùi vào dấu ngoặc của các hàm lượng giác thì xóa luôn tên hàm
               if (last=='s'||last=='c'||last=='t'||last=='g'||last=='r') {
                   expr_buf[nlen-1] = '\0';
               }
           }
       } else if(removed == ')') paren_depth++;
       calc_refresh();
       return;
   }
 
   // Nút = : Thực hiện tính toán
   if (key == '=') {
       int elen=(int)strlen(expr_buf);
       if(elen==0) return;
       char closed[EXPR_MAX+8];
       strncpy(closed,expr_buf,EXPR_MAX); closed[EXPR_MAX]='\0';
       // Tự động đóng các ngoặc còn thiếu
       for(int ci=0; ci<paren_depth && strlen(closed)<EXPR_MAX+6; ci++) strcat(closed,")");
  
       float res=sy_evaluate(closed); // Gọi thuật toán xử lý chuỗi
       last_result=res;
       calc_done=1; // Cờ báo hiệu tính toán hoàn tất
  
       char disp[128]; build_display_string(disp);
       char line0[17]={0};
       int  clen=(int)strlen(disp);
       if(clen<=15) strncpy(line0,disp,15);
       else         strncpy(line0,disp+clen-15,15);
       strcat(line0,"=");
  
       // Cảnh báo lỗi toán học
       char line1[17]={0};
       if(res >= 0.9e30f || res <= -0.9e30f) {
           strcpy(line1, "Math Error");
       } 
       else if(res >= 0.9e29f || res <= -0.9e29f) {
           strcpy(line1, "INF");
       } 
       else {
           format_display_float(res, line1); // Chuyển kết quả sang chuỗi hiển thị
       }
  
       lcd_clear();
       lcd_gotoxy(0,0); lcd_puts(line0); // Dòng 1: Biểu thức + '='
       lcd_gotoxy(0,1); lcd_puts(line1); // Dòng 2: Kết quả
  
       strcpy(expr_buf,""); paren_depth=0; // Làm sạch bộ đệm cho phép tính mới
       return;
   }
 
   // Logic Ans: Nếu vừa tính xong, ấn số thì làm phép tính mới, ấn toán tử thì lấy lại kết quả cũ (Ans)
   if (calc_done) {
       if ((key>='0' && key<='9') || key=='.' || key=='(' || key==KEY_SQRT) {
            calc_full_reset();
       } else if (key=='+' || key=='-' || key=='x' || key=='/') {
            char tmp[16]; format_display_float(last_result, tmp);
            calc_full_reset();
            strcat(expr_buf, tmp);
            calc_done = 0;
       } else {
            return;
       }
   }
 
   // Nút Căn bậc 2
   if (key == KEY_SQRT) { 
       if (calc_done) calc_full_reset();
       insert_function('r'); return; 
   }
 
   // Nút số 0-9
   if (key >= '0' && key <= '9') {
       int elen = strlen(expr_buf);
       char last = elen ? expr_buf[elen-1] : '\0';
       // Tự động thêm 'x' nếu trước số là ngoặc đóng hoặc % (VD: )5 -> )*5)
       if (last == ')' || last == '%') insert_operator('x'); 
       
       char tmp[2] = {key, '\0'};
       if(strlen(expr_buf) < EXPR_MAX - 2) strcat(expr_buf, tmp);
       calc_refresh();
   }
   // Nút dấu chấm thập phân
   else if (key == '.') {
       int elen=(int)strlen(expr_buf);
       int start=elen;
       // Tìm về ký tự bắt đầu của số thập phân hiện tại
       while(start>0 && ((expr_buf[start-1]>='0' && expr_buf[start-1]<='9') || expr_buf[start-1]=='.')) start--;
       int already=0;
       for(int si=start; si<elen; si++) if(expr_buf[si]=='.') already=1;
       // Nếu chưa có dấu chấm nào trong số này
       if(!already && strlen(expr_buf)<EXPR_MAX-2){
            char last=elen ? expr_buf[elen-1] : '\0';
            // Tự chèn '0' nếu bấm '.' đầu tiên (VD: .5 -> 0.5)
            if(elen==0 || last=='(' || last=='+' || last=='-' || last=='x' || last=='/' || last=='p' || last=='r' || last=='L') strcat(expr_buf,"0");
            strcat(expr_buf,".");
            calc_refresh();
       }
   }
   // Các toán tử 4 phép tính
   else if (key == '+' || key == '-' || key == 'x' || key == '/') {
       insert_operator(key);
   }
   // Nút ngoặc mở '('
   else if (key == '(') {
       int elen = strlen(expr_buf);
       char last = elen ? expr_buf[elen-1] : '\0';
       // Tự nối dấu nhân 'x' (VD: 5( -> 5*( )
       if ((last>='0' && last<='9') || last==')' || last=='%') insert_operator('x');
       
       if(strlen(expr_buf)<EXPR_MAX-2){
           strcat(expr_buf,"("); paren_depth++; calc_refresh();
       }
   }
   // Nút ngoặc đóng ')'
   else if (key == ')') {
       // Chỉ cho đóng khi đã có ngoặc mở
       if(paren_depth > 0 && strlen(expr_buf)<EXPR_MAX-2){
           strcat(expr_buf,")"); paren_depth--; calc_refresh();
       }
   }
}
 
/* =================================================================
 *
MAIN & MENU
 *
================================================================= */
const char *menu_items[] = {"1.Calculator", "2.Giai PT Bac 2"};
 
// Giao diện chọn Menu
void show_menu(){
   lcd_clear();
   lcd_gotoxy(0,0); lcd_puts(">"); lcd_puts(menu_items[menu_index]); // Đánh dấu > ở lựa chọn hiện tại
   lcd_gotoxy(0,1); lcd_puts(" "); lcd_puts(menu_items[(menu_index+1)%2]);
}
 
int main(void){
   lcd_init(); // Khởi tạo màn hình
   key_init(); // Khởi tạo bàn phím
 
   // Màn hình khởi động
   lcd_clear(); lcd_puts("Calculator");
   lcd_gotoxy(0,1); lcd_puts("Is Ready...");
   _delay_ms(1500);
 
   calc_full_reset();
   calc_refresh();
 
   // Các biến phục vụ chế độ Giải Phương Trình Bậc 2
   float pt_a=0, pt_b=0, pt_c=0;
   int   pt_step=0; // Bước nhập liệu: 0 = nhập a, 1 = nhập b, 2 = nhập c
   char  pt_buf[16]="";
 
   while(1){
      unsigned char key=key_scan();
      if(key==0xFF) continue; // Quay lại nếu chưa bấm phím
 
      // Nút Mode để chuyển sang Menu
      if(key==KEY_MODE){
          calc_done=0; menu_mode=0; menu_index=0;
          show_menu();
          continue;
      }
 
      // Nếu đang trong Menu (Mode 0)
      if(menu_mode==0){
          if(key==KEY_DOWN) { // Nút lên xuống để chọn
               menu_index=(menu_index+1)%2; show_menu(); 
          }
          else if(key=='='){  // Bấm '=' để xác nhận chế độ
               menu_mode=menu_index+1;
               calc_done=0; lcd_clear();
               if(menu_mode==1){ calc_full_reset(); calc_refresh(); } // Chọn máy tính thường
               else if(menu_mode==2){ // Chọn giải phương trình
                   pt_step=0; pt_a=0; pt_b=0; pt_c=0; strcpy(pt_buf,"");
                   lcd_puts("a=");
               }
          }
      }
      // Mode 1: Chế độ máy tính khoa học (đã viết ở hàm calc_handle)
      else if(menu_mode==1){ calc_handle(key); }
      // Mode 2: Chế độ Giải phương trình bậc 2 (ax^2 + bx + c = 0)
      else if(menu_mode==2){
          if(key==KEY_ON_C){ // AC / Backspace
               strcpy(pt_buf,""); lcd_clear();
               if(pt_step==0)     lcd_puts("a=");
               else if(pt_step==1) lcd_puts("b=");
               else                lcd_puts("c=");
          }
          else if((key>='0' && key<='9') || key=='.' || key=='-'){ // Nhập liệu hệ số
               generic_input(pt_buf,key); lcd_clear();
               if(pt_step==0)     lcd_puts("a=");
               else if(pt_step==1) lcd_puts("b=");
               else                lcd_puts("c=");
               lcd_puts(pt_buf); // Hiển thị hệ số đang gõ
          }
          else if(key=='='){ // Bấm = để chốt hệ số
               if(pt_step==0){
                    pt_a=atof(pt_buf); // Chuyển chuỗi thành Float
                    if(pt_a==0.0f){ // Cảnh báo nếu a=0 (không phải PT bậc 2)
                        lcd_clear(); lcd_puts("a != 0 !"); _delay_ms(1500); 
                        strcpy(pt_buf,""); lcd_clear(); lcd_puts("a="); 
                    } 
                    else { pt_step=1; strcpy(pt_buf,""); lcd_clear(); lcd_puts("b="); } // Chuyển qua bước nhập B
               } else if(pt_step==1){
                    pt_b=atof(pt_buf); pt_step=2; strcpy(pt_buf,""); lcd_clear(); lcd_puts("c="); // Chuyển qua bước nhập C
                } else if(pt_step==2){
                    pt_c=atof(pt_buf);
                    
                    // Tính Delta = b^2 - 4ac
                    float delta=pt_b*pt_b-4.0f*pt_a*pt_c;
                    lcd_clear();
                    char tmp[16];
                    if(delta<0.0f){ // Vô nghiệm (nghiệm phức)
                        lcd_puts("No real roots"); 
                    }
                    else if(delta==0.0f){ // Nghiệm kép
                        float x0=-pt_b/(2.0f*pt_a);
                        lcd_puts("x="); format_display_float(x0, tmp); lcd_puts(tmp);
                    } else { // 2 nghiệm phân biệt
                        float sq=(float)sqrt((double)delta);
                        float x1=(-pt_b+sq)/(2.0f*pt_a); 
                        float x2=(-pt_b-sq)/(2.0f*pt_a);
                        lcd_puts("x1="); format_display_float(x1, tmp); lcd_puts(tmp);
                        lcd_gotoxy(0,1); lcd_puts("x2="); format_display_float(x2, tmp); lcd_puts(tmp);
                    }
                    _delay_ms(3000); // Đợi 3s hiển thị kết quả
                    
                    // Reset lại quá trình từ đầu (nhập a)
                    pt_step=0; pt_a=0; pt_b=0; pt_c=0;
                    strcpy(pt_buf,""); lcd_clear(); lcd_puts("a=");
               }
          }
      }
   }
}
