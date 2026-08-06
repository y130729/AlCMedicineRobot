#include "common.h"
#include "stdlib.h"
#include "wifista.h"
#include "lcd.h"

//ATK-ESP8266 WIFI STA模式
//用于连接TCP/UDP服务器
//返回值:0,成功
//    其他,失败
//void atk_8266_wifista_config(void)
//{
//

//		while(atk_8266_send_cmd("AT+RST","OK",50))
//	{
//	  //	printf("重启失败，再次尝试\r\n");
//		  LCD_ShowString(30,170,210,24,16,"     ");
//			LCD_ShowString(30,170,210,24,16,"RST");
//
//	  delay_ms(1000);
//	}
//
//	while(atk_8266_send_cmd("AT+CWMODE=1","OK",50))
//	{
//	//	printf("设置sta模式失败，准备重试\r\n");
//		LCD_ShowString(30,170,210,24,16,"     ");
//		LCD_ShowString(30,170,210,24,16,"CWMODE");
//	  delay_ms(1000);
//	}
//
//	while(atk_8266_send_cmd("AT+CIPMUX=0","OK",200))
//	{
//	 //  printf("设置连接失败，再次尝试  \r\n");
//
//		LCD_ShowString(30,170,210,24,16,"     ");
//		LCD_ShowString(30,170,210,24,16,"CIPMUX");
//	  delay_ms(200);
//	}

//	while(atk_8266_send_cmd("AT+CWJAP=\"mywifi\",\"123456789\"","WIFI GOT IP",600))
//	{
//
//		LCD_ShowString(30,170,210,24,16,"     ");
//		LCD_ShowString(30,170,210,24,16,"CWJAP");
//	 //  printf("连接热点失败，再次尝试 \r\n");
//
//	  delay_ms(1000);
//	}
//
//
//	while(atk_8266_send_cmd("AT+CIPSTART=\"TCP\",\"192.168.43.189\",8086","OK",1000))
//  {
//	 //  printf(" 连接服务器失败再次尝试        \r\n");
//			LCD_ShowString(30,170,210,24,16,"     ");
//		LCD_ShowString(30,170,210,24,16,"CIPSTART");
//	  delay_ms(200);
//	}
//
//	atk_8266_send_cmd("AT+CIPMODE=1","OK",200);      //设置模式为透传
//	delay_ms(200);
////	printf("%s\r\n","connect success");
//		LCD_ShowString(30,170,210,24,16,"             ");
//		LCD_ShowString(30,170,210,24,16,"connect success");
//	atk_8266_send_cmd("AT+CIPSEND","OK",200);      //开启透传模式
//	delay_ms(200);
//
//
//
//}


void atk_8266_wifista_config(void)
{


while(atk_8266_send_cmd("AT+RST","OK",50))
	{
		  LCD_ShowString(30,170,210,24,16,"     ");
			LCD_ShowString(30,170,210,24,16,"RST");

	  delay_ms(1000);
	}

	while(atk_8266_send_cmd("AT+CWMODE=1","OK",50))
	{

		LCD_ShowString(30,170,210,24,16,"     ");
		LCD_ShowString(30,170,210,24,16,"CWMODE");
	  delay_ms(1000);
	}



	while(atk_8266_send_cmd("AT+CWJAP=\"ChinaNet-328D\",\"wh37kz37\"","WIFI GOT IP",600))
	{

		LCD_ShowString(30,170,210,24,16,"     ");
		LCD_ShowString(30,170,210,24,16,"CWJAP");
	  delay_ms(1000);
	}

		while(atk_8266_send_cmd("AT+CIPMUX=0","OK",200))
	{

		LCD_ShowString(30,170,210,24,16,"     ");
		LCD_ShowString(30,170,210,24,16,"CIPMUX");
	  delay_ms(200);
	}


	while(atk_8266_send_cmd("AT+CIPSTART=\"TCP\",\"192.168.2.137\",8086","OK",1000))
  {

			LCD_ShowString(30,170,210,24,16,"     ");
		LCD_ShowString(30,170,210,24,16,"CIPSTART");
	  delay_ms(200);
	}

	atk_8266_send_cmd("AT+CIPMODE=1","OK",200);      //设置模式为透传
	delay_ms(200);

		LCD_ShowString(30,170,210,24,16,"             ");
		LCD_ShowString(30,170,210,24,16,"connect success");
	atk_8266_send_cmd("AT+CIPSEND","OK",200);      //开启透传模式
	delay_ms(200);

}
