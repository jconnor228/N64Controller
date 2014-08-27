/*
 * RF_Tranceiver.c
 *
 * Created: 2012-08-10 15:24:35
 *  Author: Kalle
 *  Atmega88
 */ 
 
#include <avr/io.h>
#include <stdio.h>
#define F_CPU 16000000UL  // 16 MHz
#include <util/delay.h>
#include <avr/interrupt.h>
 
#include "nRF24L01.h"
 
#define dataLen 3  //length of data packet sent / received
uint8_t *data;
uint8_t *arr;
 
 
/*****************�ndrar klockan till 8MHz ist f�r 1MHz*****************************/
void clockprescale(void)	
{
	CLKPR = 0b10000000;	//Prepare the chip for a change of clock prescale (CLKPCE=1 and the rest zeros)
	CLKPR = 0b00000000;	//Wanted clock prescale (CLKPCE=0 and the four first bits CLKPS0-3 sets division factor = 1)
	//See page 38 in datasheet
}
////////////////////////////////////////////////////
 
 
/*****************USART*****************************/  //Skickar data fr�n chip till com-port simulator p� datorn
//Initiering
 
void usart_init(void)
{
	DDRD |= (1<<1);	//Set TXD (PD1) as output for USART
	
	unsigned int USART_BAUDRATE = 9600;		//Same as in "terminal.exe"
	unsigned int ubrr = (((F_CPU / (USART_BAUDRATE * 16UL))) - 1);	//baud prescale calculated according to F_CPU-define at top
	
	/*Set baud rate */
	UBRR0H = (unsigned char)(ubrr>>8);
	UBRR0L = (unsigned char)ubrr;
	
	/*	Enable receiver and transmitter */
	UCSR0B = (1<<RXEN0)|(1<<TXEN0);
 
	/* Set frame format: 8data, 2stop bit, The two stop-bits does not seem to make any difference in my case!?*/
	UCSR0C = (1<<USBS0)|(3<<UCSZ00);
	
}
 
//Funktionen som skickar iv�g byten till datorn
 
void USART_Transmit(uint8_t data)
{
	/* Wait for empty transmit buffer */
	while ( !( UCSR0A & (1<<UDRE0)) );
	/* Put data into buffer, sends the data */
	UDR0 = data;
}
 
//Funktionen som Tar emot kommandon av datorn som senare ska skickas till transmittern
 
 
uint8_t USART_Receive( void )
{
	/* Wait for data to be received */
	while ( !(UCSR0A & (1<<RXC0)) );	//This loop is only needed if you not use the interrupt...
	
	/* Get and return received data from buffer */
	return UDR0; //Return the received byte
}
 
/*****************SPI*****************************/  //Skickar data mellan chip och nrf'ens chip
//initiering
void InitSPI(void)
{
	//Set SCK (PB5), MOSI (PB3) , CSN (SS & PB2) & C  as outport 
	//OBS!!! M�ste s�ttas innan SPI-Enable neadn
	DDRB |= (1<<DDB5) | (1<<DDB3) | (1<<DDB2) |(1<<DDB1);
	
	/* Enable SPI, Master, set clock rate fck/16 .. kan �ndra hastighet utan att det g�r s� mycket*/
	SPCR |= (1<<SPE)|(1<<MSTR);// |(1<<SPR0) |(1<<SPR1);
	
	SETBIT(PORTB, 2);	//CSN IR_High to start with, vi ska inte skicka n�t till nrf'en �nnu!
	CLEARBIT(PORTB, 1);	//CE low to start with, nrf'en ska inte s�nda/ta emot n�t �nnu!
}
 
//Skickar kommando till nrf'en � f�r d� tillbaka en byte
char WriteByteSPI(unsigned char cData)
{
	//Load byte to Data register
	SPDR = cData;	
		
	/* Wait for transmission complete */
	while(!(SPSR & (1<<SPIF)));
	
	//Returnera det som s�nts tillbaka av nrf'en (f�rsta g�ngen efter csn-l�g kommer Statusregistert)
	return SPDR;
}
////////////////////////////////////////////////////
 
 
/*****************in/out***************************/  //st�ll in t.ex. LED
//s�tter alla I/0 portar f�r t.ex. LED
void ioinit(void)			
{
	DDRB |= (1<<DDB0); //led
}
////////////////////////////////////////////////////
 
 
/*****************interrupt***************************/ //orsaken till att k�ra med interrupt �r att de avbryter koden var den �n �r och k�r detta som �r viktigast!
//n�r data tas emot/skickas s� g�r interr uptet INT0 n�st l�ngst ner ig�ng
void INT0_interrupt_init(void)	
{
	DDRD &= ~(1 << DDD2);	//Extern interrupt p� INT0, dvs s�tt den till input!
	
	EICRA |=  (1 << ISC01);// INT0 falling edge	PD2
	EICRA  &=  ~(1 << ISC00);// INT0 falling edge	PD2
 
	EIMSK |=  (1 << INT0);	//enablar int0
  	//sei();              // Enable global interrupts g�rs sen
} 
 
//n�r chipets RX (usart) f�r ett meddelande f�rn datorn g�r interruptet USART_RX ig�ng l�ngst ner.
 
void USART_interrupt_init(void)
{
	UCSR0B |= (1<<RXCIE0);	//Enable interrupt that triggers on USART-data is received,
}
 
//////////////////////////////////////////////////////
 
//funktion f�r att h�mta n�t av nrf's register
uint8_t GetReg(uint8_t reg)
{	
	//andv�ndning: USART_Transmit(GetReg(STATUS)); //d�r status �r registret du vill kolla
	_delay_us(10);
	CLEARBIT(PORTB, 2);	//CSN low
	_delay_us(10);
	WriteByteSPI(R_REGISTER + reg);	//Vilket register vill du l�sa (nu med R_Register f�r att inget ska skrivas till registret)
	_delay_us(10);
	reg = WriteByteSPI(NOP);	//Skicka NOP antalet byte som du vill h�mta (oftast 1g�ng, men t.ex addr �r 5 byte!) och spara isf inte i "reg" utan en array med en loop
	_delay_us(10);
	SETBIT(PORTB, 2);	//CSN IR_High
	return reg;	// Returnerar registret f�rhoppningsvis med bit5=1 (tx_ds=lyckad s�ndning)
}
 
 
/*****************nrf-setup***************************/ //St�ller in nrf'en genoma att f�rst skicka vilket register, sen v�rdet p� registret.
uint8_t *WriteToNrf(uint8_t ReadWrite, uint8_t reg, uint8_t *val, uint8_t antVal)	//tar in "ReadWrite" (W el R), "reg" (ett register), "*val" (en array) & "antVal" (antal integer i variabeln)
{
	cli();	//disable global interrupt
	
	if (ReadWrite == W)	//W=vill skriva till nrf-en (R=l�sa av den, R_REGISTER (0x00) ,s� skiter i en else funktion)
	{
		reg = W_REGISTER + reg;	//ex: reg = EN_AA: 0b0010 0000 + 0b0000 0001 = 0b0010 0001  
	}
	
	//Static uint8_t f�r att det ska g� att returnera en array (l�gg m�rke till "*" uppe p� funktionen!!!)
	static uint8_t ret[dataLen];	//antar att det l�ngsta man vill l�sa ut n�r man kallar p� "R" �r dataleng-l�ngt, dvs anv�nder man bara 1byte datalengd � vill l�sa ut 5byte RF_Adress s� skriv 5 h�r ist!!!	
	
	_delay_us(10);		//alla delay �r s� att nrfen ska hinna med! (microsekunder)
	CLEARBIT(PORTB, 2);	//CSN low = nrf-chippet b�rjar lyssna
	_delay_us(10);		
	WriteByteSPI(reg);	//f�rsta SPI-kommandot efter CSN-l�g ber�ttar f�r nrf'en vilket av dess register som ska redigeras ex: 0b0010 0001 write to registry EN_AA	
	_delay_us(10); 		
	
	int i;
	for(i=0; i<antVal; i++)
	{
		if (ReadWrite == R && reg != W_TX_PAYLOAD)
		{
			ret[i]=WriteByteSPI(NOP);	//Andra och resten av SPI kommandot s�ger �t nrfen vilka v�rden som i det h�r fallet ska l�sas
			_delay_us(10);			
		}
		else 
		{
			WriteByteSPI(val[i]);	//Andra och resten av SPI kommandot s�ger �t nrfen vilka v�rden som i det h�r fallet ska skrivas till
			_delay_us(10);
		}		
	}
	SETBIT(PORTB, 2);	//CSN IR_High = nrf-chippet slutar lyssna
	
	sei(); //enable global interrupt
	
	return ret;	//returnerar en array
}
 
//initierar nrf'en (obs nrfen m�ste vala i vila n�r detta sker CE-l�g)
void nrf24L01_init(void)
{
	_delay_ms(100);	//allow radio to reach power down if shut down
	
	uint8_t val[5];	//an array of integers to send to writetonrf function
 
	//EN_AA - (enable auto-acknowledgements) - transmitter gets automatic response from reciever on successful transmit
	//only works if transmitter has identical rf address on its channel ex: RX_ADDR_P0 = TX_ADDR
	val[0]=0x01;	//set value
	WriteToNrf(W, EN_AA, val, 1);	//W=write mode, EN_AA=register to write to, val=data to write, 1=number of bytes
	
	//SETUP_RETR (the setup for "EN_AA")
	val[0]=0x2F;	//0b0010 00011 "2" sets it up to 750uS delay between every retry (at least 500us at 250kbps and if payload >5bytes in 1Mbps, and if payload >15byte in 2Mbps) "F" is number of retries (1-15, now 15)
	WriteToNrf(W, SETUP_RETR, val, 1);
	
	//setup the number of enabled data pipes (1-5)
	val[0]=0x01;
	WriteToNrf(W, EN_RXADDR, val, 1); //enable data pipe 0
 
	//RF_Adress width setup (how many bytes is the receiver address-the more the merrier 1-5)
	val[0]=0x03;
	WriteToNrf(W, SETUP_AW, val, 1); //0b0000 00011 5byte RF_Adress
 
	//RF channel setup - choose freq 2.400-2.527GHz 1MHz/step
	val[0]=0x01;
	WriteToNrf(W, RF_CH, val, 1); //RF channel registry 0b0000 0001 = 2.401GHz (same on TX and RX)
 
	//RF setup	- choose power mode and data speed 
	val[0]=0x07;
	WriteToNrf(W, RF_SETUP, val, 1); //00000111 bit 3="0" ger l�gre �verf�ringshastighet 1Mbps=L�ngre r�ckvidd, bit 2-1 ger effektl�ge h�g (-0dB) ("11"=(-18dB) ger l�gre effekt =str�msn�lare men l�gre range)
 
	//RX RF_Adress setup 5 byte - v�ljer RF_Adressen p� Recivern (M�ste ges samma RF_Adress om Transmittern har EN_AA p�slaget!!!)
	int i;
	for(i=0; i<5; i++)	
	{
		val[i]=0x12;	//RF channel registry 0b10101011 x 5 - skriver samma RF_Adress 5ggr f�r att f� en l�tt och s�ker RF_Adress (samma p� transmitterns chip!!!)
	}
	WriteToNrf(W, RX_ADDR_P0, val, 5); //0b0010 1010 write registry - eftersom vi valde pipe 0 i "EN_RXADDR" ovan, ger vi RF_Adressen till denna pipe. (kan ge olika RF_Adresser till olika pipes och d�rmed lyssna p� olika transmittrar) 	
	
	//TX RF_Adress setup 5 byte -  v�ljer RF_Adressen p� Transmittern (kan kommenteras bort p� en "ren" Reciver)
	//int i; //�teranv�nder f�reg�ende i...
	for(i=0; i<5; i++)	
	{
		val[i]=0x12;	//RF channel registry 0b10111100 x 5 - skriver samma RF_Adress 5ggr f�r att f� en l�tt och s�ker RF_Adress (samma p� Reciverns chip och p� RX-RF_Adressen ovan om EN_AA enablats!!!)
	}
	WriteToNrf(W, TX_ADDR, val, 5); 
 
	// payload width setup - Hur m�nga byte ska skickas per s�ndning? 1-32byte 
	val[0]=dataLen;		//"0b0000 0001"=1 byte per 5byte RF_Adress  (kan v�lja upp till "0b00100000"=32byte/5byte RF_Adress) (definierat h�gst uppe i global variabel!)
	WriteToNrf(W, RX_PW_P0, val, 1);
	
	//CONFIG reg setup - Nu �r allt inst�llt, boota upp nrf'en och g�r den antingen Transmitter lr Reciver
	val[0]=0x1E;  //0b0000 1110 config registry	bit "1":1=power up,  bit "0":0=transmitter (bit "0":1=Reciver) (bit "4":1=>mask_Max_RT,dvs IRQ-vektorn reagerar inte om s�ndningen misslyckades. 
	WriteToNrf(W, CONFIG, val, 1);
 
//device need 1.5ms to reach standby mode
	_delay_ms(100);	
 
	//sei();	
}
 
void ChangeAddress(uint8_t adress)
{
	_delay_ms(100);
	uint8_t val[5];
	//RX RF_Adress setup 5 byte - v�ljer RF_Adressen p� Recivern (M�ste ges samma RF_Adress om Transmittern har EN_AA p�slaget!!!)
	int i;
	for(i=0; i<5; i++)
	{
		val[i]=adress;	//RF channel registry 0b10101011 x 5 - skriver samma RF_Adress 5ggr f�r att f� en l�tt och s�ker RF_Adress (samma p� transmitterns chip!!!)
	}
	WriteToNrf(W, RX_ADDR_P0, val, 5); //0b0010 1010 write registry - eftersom vi valde pipe 0 i "EN_RXADDR" ovan, ger vi RF_Adressen till denna pipe. (kan ge olika RF_Adresser till olika pipes och d�rmed lyssna p� olika transmittrar)
	
	//TX RF_Adress setup 5 byte -  v�ljer RF_Adressen p� Transmittern (kan kommenteras bort p� en "ren" Reciver)
	//int i; //�teranv�nder f�reg�ende i...
	for(i=0; i<5; i++)
	{
		val[i]=adress;	//RF channel registry 0b10111100 x 5 - skriver samma RF_Adress 5ggr f�r att f� en l�tt och s�ker RF_Adress (samma p� Reciverns chip och p� RX-RF_Adressen ovan om EN_AA enablats!!!)
	}
	WriteToNrf(W, TX_ADDR, val, 5);
	_delay_ms(100);
}
/////////////////////////////////////////////////////
 
/*****************Funktioner***************************/ //Funktioner som anv�nds i main
//Resettar nrf'en f�r ny kommunikation
void reset(void)
{
	_delay_us(10);
	CLEARBIT(PORTB, 2);	//CSN low
	_delay_us(10);
	WriteByteSPI(W_REGISTER + STATUS);	//
	_delay_us(10);
	WriteByteSPI(0b01110000);	//radedrar alla irq i statusregistret (f�r att kunna lyssna igen)
	_delay_us(10);
	SETBIT(PORTB, 2);	//CSN IR_High
}
 
//Reciverfunktioner
/*********************Reciverfunktioner********************************/
//�ppnar Recivern och "Lyssnar" i 1s
void receive_payload(void)
{
	sei();		//Enable global interrupt
	
	SETBIT(PORTB, 1);	//CE IR_High = "Lyssnar" 
	_delay_ms(1000);	//lyssnar i 1s och om mottaget g�r int0-interruptvektor ig�ng
	CLEARBIT(PORTB, 1); //ce l�g igen -sluta lyssna
	
	cli();	//Disable global interrupt
}
 
//S�nd data
void transmit_payload(uint8_t * W_buff)
{
	WriteToNrf(R, FLUSH_TX, W_buff, 0); //skickar 0xE1 som flushar registret f�r att gammal data inte ska ligga � v�nta p� att bli skickad n�r man vill skicka ny data! R st�r f�r att W_REGISTER inte ska l�ggas till. skickar inget kommando efterr�t eftersom det inte beh�vs! W_buff[]st�r bara d�r f�r att en array m�ste finnas d�r...
	
	WriteToNrf(R, W_TX_PAYLOAD, W_buff, dataLen);	//skickar datan i W_buff till nrf-en (obs g�r ej att l�sa w_tx_payload-registret!!!)
	
	sei();	//enable global interrupt- redan p�!
	//USART_Transmit(GetReg(STATUS));
 
	_delay_ms(10);		//beh�ver det verkligen vara ms � inte us??? JAAAAAA! annars funkar det inte!!!
	SETBIT(PORTB, 1);	//CE h�g=s�nd data	INT0 interruptet k�rs n�r s�ndningen lyckats och om EN_AA �r p�, ocks� svaret fr�n recivern �r mottagen
	_delay_us(20);		//minst 10us!
	CLEARBIT(PORTB, 1);	//CE l�g
	_delay_ms(10);		//beh�ver det verkligen vara ms � inte us??? JAAAAAA! annars funkar det inte!!!
 
	//cli();	//Disable global interrupt... ajabaja, d� st�ngs USART_RX-lyssningen av!
 
}
 
 
/////////////////////////////////////////////////////
 
/*int main(void)
{
	clockprescale();
	usart_init();
	InitSPI();
    ioinit();
	INT0_interrupt_init();
	USART_interrupt_init();
 
	nrf24L01_init();
 
	SETBIT(PORTB,0);		//F�r att se att dioden fungerar!
	_delay_ms(1000);
	CLEARBIT(PORTB,0);
	
	while(1)
	{
		//Wait for USART-interrupt to send data...
 
	}
	return 0;
}
 */
 
 
 
ISR(INT0_vect)	//vektorn som g�r ig�ng n�r transmit_payload lyckats s�nda eller n�r receive_payload f�tt data OBS: d� Mask_Max_rt �r satt i config registret s� g�r den inte ig�ng n�r MAX_RT �r uppn�d � s�ndninge nmisslyckats!
{
	cli();	//Disable global interrupt
	CLEARBIT(PORTB, 1);		//ce l�g igen -sluta lyssna/s�nda
	
	SETBIT(PORTB, 0); //led on
	_delay_ms(500);
	CLEARBIT(PORTB, 0); //led off
	
	//Receiver function to print out on usart:
	//data=WriteToNrf(R, R_RX_PAYLOAD, data, dataLen);	//l�s ut mottagen data
	//reset();
//
	//for (int i=0;i<dataLen;i++)
	//{
		//USART_Transmit(data[i]);
	//}
	//
	sei();
 
}
 
ISR(USART_RX_vect)	///Vector that triggers when computer sends something to the Atmega88
{
	uint8_t W_buffer[dataLen];	//Creates a buffer to receive data with specified length (ex. dataLen = 5 bytes)
	
	int i;
	for (i=0;i<dataLen;i++)
	{
		W_buffer[i]=USART_Receive();	//receive the USART
		USART_Transmit(W_buffer[i]);	//Transmit the Data back to the computer to make sure it was correctly received
		//This probably should wait until all the bytes is received, but works fine in to send and receive at the same time... =)
	}
	
	reset();	//reset irq - kan skicka data p� nytt
	
	if (W_buffer[0]=='9')	//om projektorduk
	{
		ChangeAddress(0x13);	//change address to send to different receiver
		transmit_payload(W_buffer);	//S�nder datan
		ChangeAddress(0x12);	//tillbaka till ultimata fj�rrisen
	} 
	else
	{
		transmit_payload(W_buffer);	//S�nder datan
	}
	
	USART_Transmit('#');	//visar att chipet mottagit datan...
	
 
}