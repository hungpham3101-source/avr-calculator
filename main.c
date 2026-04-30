#define F_CPU 8000000UL

#include <avr/io.h>
#include <util/delay.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

#define set_bit(reg,pos) reg|=(1<<pos)		// used to set bit x on register x to 1
#define clr_bit(reg,pos) reg&=~(1<<pos)		// used to clear bit x on register x to 0
#define tog_bit(reg,pos) reg^=(1<<pos)		// used to toggle bit x on register x from x to x`

void port(char data){
	if(data&1) set_bit(PORTB,0); else clr_bit(PORTB,0);
	if(data&2) set_bit(PORTB,1); else clr_bit(PORTB,1);
	if(data&4) set_bit(PORTB,2); else clr_bit(PORTB,2);
	if(data&8) set_bit(PORTB,3); else clr_bit(PORTB,3);
}

void mode_select(char m){
	if(m==0) clr_bit(PORTB,5);
	else set_bit(PORTB,5);
}

void enb_tri(){
	set_bit(PORTB,4);
	_delay_us(50);
	clr_bit(PORTB,4);
	_delay_us(200);
}

void lcd_send(char data,char mode){
	mode_select(mode);
	port(data>>4);
	enb_tri();
	port(data & 0x0F);
	enb_tri();
}

void lcd_cmd(char cmd){ lcd_send(cmd,0); }
void lcd_data(char data){ lcd_send(data,1); }

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

void lcd_gotoxy(int x,int y){
	int addr = (y==0)?0x00:0x40;
	lcd_cmd(0x80 + addr + x);
}

void lcd_puts(char *s){
	while(*s) lcd_data(*s++);
}

/* ================= KEYPAD ================= */
void key_init(){
	DDRA = 0x0F;
	PORTA = 0xFF;
}

unsigned char key_scan(){
	unsigned char cnt, key;

	for(cnt=0; cnt<4; cnt++){
		PORTA = 0xFF;
		PORTA &= ~(1<<cnt);

		key = PINA & 0xF0;

		if(key != 0xF0){
			while((PINA & 0xF0)!=0xF0);
			break;
		}
	}

	if(cnt==4) return 0xFF;

	switch(cnt){
		case 0:
		if(key==0xE0) return 0;
		if(key==0xD0) return 1;
		if(key==0xB0) return 2;
		if(key==0x70) return 3;
		break;
		case 1:
		if(key==0xE0) return 4;
		if(key==0xD0) return 5;
		if(key==0xB0) return 6;
		if(key==0x70) return 7;
		break;
		case 2:
		if(key==0xE0) return 8;
		if(key==0xD0) return 9;
		if(key==0xB0) return 10;
		if(key==0x70) return 11;
		break;
		case 3:
		if(key==0xE0) return 12;
		if(key==0xD0) return 13;
		if(key==0xB0) return 14;
		if(key==0x70) return 15;
		break;
	}

	return 0xFF;
}

#define dp 5
int i, j, row, len = 0;
char display_output[32];
float first_num = 0, second_num = 0, result_flag = 0;
int operation = 0;
char number_data[][10] = {
	"7","4","1","CLEAR",
	"8","5","2","0",
	"9","6","3","=",
	"/","x","-","+"
};
int numbers[11] = {
	7,4,1,0,
	8,5,2,0,
	9,6,3
};

void display(int n){
	switch(n) {
		case 0:
		case 1:
		case 2:
		case 4:
		case 5:
		case 6:
		case 7:
		case 8:
		case 9:
		case 10:
		if(!operation) {
			if(!len) {
				first_num = numbers[n];
			}else {
				if(!result_flag) {
					first_num = (first_num * 10) + numbers[n];
				}else {
					strcpy(display_output, "");
					len = 0;
					operation = 0;
					first_num = numbers[n];
					second_num = 0;
					result_flag = 0;
					len  = 0;
				}
			}
		}else{
			if(!len) {
			second_num = numbers[n];
		}else {
			second_num = (second_num * 10) + numbers[n];
		}
	}
	
	len++;
	strcat(display_output, number_data[n]);
	break;
	
	case 11:
	if(len) {
		if(operation) {
			switch(operation) {
				case 15:
				first_num = first_num + second_num;
				break;
				case 14:
				first_num = first_num - second_num;
				break;
				case 13:
				first_num = first_num * second_num;
				break;
				case 12:
				if(second_num != 0) first_num /= second_num;
				break;
			}
			result_flag = 1;
		}
		
		operation = 0;
		strcpy(display_output, "");
		//itoa(first_num, display_output);
		dtostrf(first_num,0,dp,display_output);
		len = 0;
	}
	break;
	
	case 3:
	strcpy(display_output, "");
	len = 0;
	operation = 0;
	first_num = 0;
	second_num = 0;
	result_flag = 0;
	break;
	case 12:
	case 13:
	case 14:
	case 15:
	if(len) {
		strcat(display_output, number_data[n]);
		operation = n;
		len = 0;
	}
	break;
} 

lcd_clear();
lcd_gotoxy(0, 0);
lcd_puts(display_output);
}

/* ================= MENU ================= */
int menu_mode = 1;
int menu_index = 0;

void show_menu(){
	lcd_clear();

	if(menu_index==0){
		lcd_puts(">Calc");
		lcd_gotoxy(0,1);
		lcd_puts(" Log");
	}
	else if(menu_index==1){
		lcd_puts(">Log");
		lcd_gotoxy(0,1);
		lcd_puts(" PT2");
	}
	else{
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

	float a=0,b=0,c=0;
	int step=0;

	while(1){
		unsigned char key = key_scan();
		if(key==0xFF) continue;

		// MENU
		if(menu_mode==1){

			if(key==2){
				menu_index=(menu_index+1)%3;
				show_menu();
			}
			else if(key==8){
				if(menu_index==0) menu_index=2;
				else menu_index--;
				show_menu();
			}
			else if(key==11){
				menu_mode=menu_index+2;
				lcd_clear();
			}
		}

		// CALC
		else if(menu_mode==2){
			display(key);

			if(key==3){
				menu_mode=1;
				show_menu();
			}
		}

		// LOG
		else if(menu_mode==3){
			if(key<=10) display(key);

			else if(key==11){
				float result = log10(first_num);
				lcd_clear();
				dtostrf(result,0,2,display_output);
				lcd_puts("log=");
				lcd_puts(display_output);
			}

			else if(key==3){
				menu_mode=1;
				show_menu();
			}
		}

		// PT BAC 2
		else if(menu_mode==4){

			if(key<=10){
				display(key);

				if(step==0) a=first_num;
				else if(step==1) b=first_num;
				else if(step==2) c=first_num;
			}

			else if(key==12){ // next
				step++;
				first_num=0;
				len=0;
				lcd_clear();

				if(step==1) lcd_puts("b=");
				else if(step==2) lcd_puts("c=");
			}

			else if(key==11){
				float delta=b*b-4*a*c;
				lcd_clear();

				if(delta<0){
					lcd_puts("No root");
				}
				else if(delta==0){
					float x=-b/(2*a);
					dtostrf(x,0,2,display_output);
					lcd_puts("x=");
					lcd_puts(display_output);
				}
				else{
					float x1=(-b+sqrt(delta))/(2*a);
					float x2=(-b-sqrt(delta))/(2*a);

					lcd_puts("x1=");
					dtostrf(x1,0,2,display_output);
					lcd_puts(display_output);

					lcd_gotoxy(0,1);
					lcd_puts("x2=");
					dtostrf(x2,0,2,display_output);
					lcd_puts(display_output);
				}

				step=0;
			}

			else if(key==3){
				menu_mode=1;
				show_menu();
			}
		}
	}
}
