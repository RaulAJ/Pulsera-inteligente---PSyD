#include <s3c44b0x.h>
#include <s3cev40.h>
#include <common_types.h>
#include <leds.h>
#include <system.h>
#include <lcd.h>
#include <ts.h>
#include "sensorsEmulator.h"
#include <rtc.h>
#include <timers.h>
#include <uda1341ts.h>
#include <iis.h>
#include <pbs.h>
#include <fix_types.h>
#include <keypad.h>
#include <segs.h>

#define MAX_PULSACIONES (200) //umbral de pulsaciones para alerta
#define TICKS_PER_SEC   (100)
#define Q (8)

#define NOKIATUNE_SIZE  (352000)  /* (5,5 s) * (2 canales) * (2 B/canal) * (16000 muestras/s) = 352000 B */
#define DTMF_SIZE       (6400)

#define NOKIATUNE      ((int16 *)0x0c402000)
#define DTMF0      ((int16 *)0x0c400000)


static void beatHandler( void ) __attribute__ ((interrupt ("IRQ")));
static void stepHandler( void ) __attribute__ ((interrupt ("IRQ")));

void isr_ts( void ) __attribute__ ((interrupt ("IRQ")));
void isr_pbs( void ) __attribute__ ((interrupt ("IRQ")));
void isr_tick( void ) __attribute__ ((interrupt ("IRQ")));

/*******************************************************************/

void readBeats ( uint16 *beatsPerMinute );
void readSteps ( uint16 *stepsPerMinute );
boolean checkAlarm (  rtc_time_t rtc_time,  rtc_time_t alarm );
void activaAlertaAlarma ( rtc_time_t alarma );
void pintaDistancia ( ufix32 distanciaAndada );
void pintaMenu ( rtc_time_t rtc_time, rtc_time_t *alarma, ufix32 *distanciaPasos);
void put_time_x2 ( rtc_time_t rtc_time );
void pinta_alarma( rtc_time_t alarma );
rtc_time_t menuAlarma ( void );
void menuDistanciaPasos ( ufix32 *distanciaPasos );
void alertaPulsaciones ( uint16 beatsPerMinute );
void separateParts(ufix32 f, uint32* parte_entera, uint32* parte_decimal);
void minijuego( rtc_time_t rtc_time );

volatile static boolean newBeat = FALSE;
volatile static boolean newStep = FALSE;

volatile boolean flagTS;
volatile boolean flagPBS;
volatile boolean flagRTC;
volatile boolean flagUpdateSB;
volatile boolean flagAlarma;
volatile boolean flagFinDeAlarma;
volatile boolean flagCheckPulsaciones;

volatile uint8 nBeatsIn10Secs;
volatile uint8 nStepsIn10Secs;

/*******************************************************************/

void main( void )
{
	sys_init();
	leds_init();
	rtc_init();
	lcd_init();
	ts_init();
	uda1341ts_init();
	lcd_on();
	lcd_clear();
	ts_on();
	pbs_init();
	iis_init( IIS_DMA );
	keypad_init();

	flagUpdateSB = TRUE;
	flagRTC = FALSE;
	flagAlarma = FALSE; //activar cuando se ponga una alarma
	flagFinDeAlarma = FALSE;
	flagPBS = FALSE;
	flagTS = FALSE;
	flagCheckPulsaciones = FALSE;

	const uint8 *cadenaTiempo = (uint8*)__TIME__;

	rtc_time_t rtc_time, alarma;
	rtc_time.hour = (cadenaTiempo[0] - '0') * 10 + (cadenaTiempo[1] - '0');
	rtc_time.min = (cadenaTiempo[3] - '0') * 10 + (cadenaTiempo[4] - '0');
	rtc_time.sec = (cadenaTiempo[6] - '0') * 10 + (cadenaTiempo[7] - '0');

	rtc_puttime(&rtc_time);

	ufix32 distanciaPaso = TOFIX(ufix32, 1.0, Q);
    uint16 beatsPerMinute = 60;
    uint16 stepsPerMinute = 60;
    ufix32 distanciaAndada = 0;
	readBeats(&beatsPerMinute);
	readSteps(&stepsPerMinute);
	nBeatsIn10Secs = 0;
	nStepsIn10Secs = 0;

	timer0_open_tick( isr_tick, TICKS_PER_SEC );
	ts_open(isr_ts);
	pbs_open(isr_pbs);

	startSensorsEmulator( beatHandler, stepHandler, beatsPerMinute, stepsPerMinute );     // Arranca el emulador de sensores, instalando beatHandler y stepHandler como RTI de los respectivos sensores

	while( 1 )
    {
    	if( newBeat )
    	{
    		newBeat = FALSE;
    		led_toggle( LEFT_LED );
    	}
    	if( newStep )
    	{
    		distanciaAndada = FADD(distanciaAndada,distanciaPaso);
    		pintaDistancia(distanciaAndada);
    		newStep = FALSE;
    		led_toggle( RIGHT_LED );
    	}
    	if( flagRTC )
    	{
    		flagRTC = FALSE;
    		rtc_gettime( &rtc_time );
    		put_time_x2(rtc_time);
    		if( flagAlarma )
    		{
    			if(checkAlarm(rtc_time, alarma)){
    				flagFinDeAlarma = TRUE;
    				flagAlarma = FALSE;
    			}
    		}
    	}
    	if( flagUpdateSB )
    	{
    		stepsPerMinute = nStepsIn10Secs * 6;
    		beatsPerMinute = nBeatsIn10Secs * 6;
    		nBeatsIn10Secs = 0;
    		nStepsIn10Secs = 0;
    		flagUpdateSB = FALSE;
    		lcd_puts(0, 10, BLACK, "BPM: ");
    		lcd_putint(35, 10, BLACK, beatsPerMinute);
    		lcd_puts(65, 10, BLACK, "SPM: ");
    		lcd_putint(100, 10, BLACK, stepsPerMinute);

    	}
    	if( flagFinDeAlarma )
    	{
    		flagFinDeAlarma = FALSE;
    		activaAlertaAlarma(alarma);
    	}
    	if( flagCheckPulsaciones )
    	{
    		if(beatsPerMinute > MAX_PULSACIONES)
    		{
    			alertaPulsaciones(beatsPerMinute);
    		}
    		flagCheckPulsaciones = FALSE;
    	}
    	pintaMenu(rtc_time, &alarma, &distanciaPaso);

    };
}
void minijuego ( rtc_time_t rtc_time )
{
    lcd_clear();
    uint8 random1 = rtc_time.hour % 16;
    uint8 random2 = rtc_time.min % 16;
    uint8 random3 = rtc_time.sec % 16;
    boolean fin_partida = FALSE;
    segs_init();
    segs_putchar(random1);
    lcd_puts(61, 110, BLACK, "Pulse la tecla indicada ");
    lcd_puts(85, 126, BLACK, "en el 7 segmentos ");    uint8 respuesta = KEYPAD_FAILURE;
    while(respuesta == KEYPAD_FAILURE){
        respuesta = keypad_getchar();
    }
    if(respuesta != random1){
        fin_partida = TRUE;
        segs_off();
        lcd_puts(61, 110, WHITE, "Pulse la tecla indicada ");
        lcd_puts(85, 126, WHITE, "en el 7 segmentos ");
        lcd_puts_x2(61, 110, BLACK, "Has perdido!");
    }
    segs_putchar(random2);
    respuesta = KEYPAD_FAILURE;
    while(respuesta == KEYPAD_FAILURE && !fin_partida){
        respuesta = keypad_getchar();
    }
    if(respuesta != random2 || fin_partida){
        fin_partida = TRUE;
        segs_off();
        lcd_puts(61, 110, WHITE, "Pulse la tecla indicada ");
        lcd_puts(85, 126, WHITE, "en el 7 segmentos ");
        lcd_puts_x2(61, 110, BLACK, "Has perdido!");
    }
    segs_putchar(random3);
    respuesta = KEYPAD_FAILURE;
    while(respuesta == KEYPAD_FAILURE && !fin_partida){
        respuesta = keypad_getchar();
    }
    if(respuesta != random3 || fin_partida){
        fin_partida = TRUE;
        segs_off();
        lcd_puts(61, 110, WHITE, "Pulse la tecla indicada ");
        lcd_puts(85, 126, WHITE, "en el 7 segmentos ");
        lcd_puts_x2(61, 110, BLACK, "Has perdido!");
    }
    if(!fin_partida){
    	lcd_puts(61, 110, WHITE, "Pulse la tecla indicada ");
    	lcd_puts(85, 126, WHITE, "en el 7 segmentos ");
    	lcd_puts_x2(61, 110, BLACK, "Has ganado!");
    }
    sw_delay_s(3);

    lcd_clear();
    segs_off();
}

void put_time_x2 ( rtc_time_t rtc_time)
{
	if(rtc_time.hour < 10){
		lcd_putint_x2(100, 110, BLACK, 0);
		lcd_putint_x2(116, 110, BLACK, rtc_time.hour);
	}
	else{
		lcd_putint_x2(100, 110, BLACK, rtc_time.hour);
	}

	lcd_putchar_x2(129, 110, BLACK, ':');
	if(rtc_time.min < 10){
		lcd_putint_x2(145, 110, BLACK, 0);
		lcd_putint_x2(161, 110, BLACK, rtc_time.min);
	}
	else{
		lcd_putint_x2(145, 110, BLACK, rtc_time.min);
	}
	lcd_putchar_x2(174, 110, BLACK, ':');
	if(rtc_time.sec < 10){
		lcd_putint_x2(190, 110, BLACK, 0);
		lcd_putint_x2(206, 110, BLACK, rtc_time.sec);
	}
	else{
		lcd_putint_x2(190, 110, BLACK, rtc_time.sec);
	}
}

void pintaMenu ( rtc_time_t rtc_time, rtc_time_t *alarma, ufix32 *distanciaPasos )
{
	lcd_draw_box(20 ,40, 135, 100, BLACK, 3); //alarma
	lcd_puts_x2(30, 56, BLACK, "ALARMA");
	lcd_draw_box(20 ,150, 135, 210, BLACK, 3); //distancia pasos
	lcd_puts(44, 166, BLACK, "DISTANCIA");
	lcd_puts(59, 183, BLACK, "PASOS");
	lcd_draw_box(184 ,150, 299, 210, BLACK, 3); //valor alarma
	lcd_puts(205, 156, BLACK, "ALARMA");
	pinta_alarma(*alarma);
	lcd_draw_box(184 ,40, 299, 100, BLACK, 3); //minijuego
	lcd_puts(210, 62, BLACK, "MINIGAME");


	uint16 x, y;
	if(flagTS){
		ts_getpos(&x, &y);
		if(x >= 20 && x <= 135 && y >= 40 && y <= 100){ //alarma
			rtc_time_t a = menuAlarma();
			*alarma = a;
		}
		else if(x >= 20 && x <= 135 && y >= 150 && y <= 210){ //distancia pasos
			menuDistanciaPasos(distanciaPasos);
		}
		else if(x >= 184 && x <= 299 && y >= 150 && y <= 210){ //mostrar alarma
			flagAlarma = FALSE;
			lcd_putint_x2(210, 174, WHITE, 8);
			lcd_putint_x2(218, 174, WHITE, 8);
			lcd_putchar_x2(225, 174, WHITE, ':');
			lcd_putint_x2(232, 174, WHITE, 8);
			lcd_putint_x2(240, 174, WHITE, 8);
			lcd_putchar_x2(247, 174, WHITE, ':');
			lcd_putint_x2(254, 174, WHITE, 8);
			lcd_putint_x2(263, 174, WHITE, 8);
		}
		else if(x >= 184 && x <= 299 && y >= 40 && y <= 100){ //minijuego
			minijuego(rtc_time);
		}
		flagTS = FALSE;
	}

}

void menuDistanciaPasos ( ufix32 *distanciaPaso )
{
	lcd_clear();

	ufix32 distanciaPasos = TOFIX(ufix32, 1.0, Q);
	uint32 metros, decimales;
	while(1){
		separateParts(distanciaPasos, &metros, &decimales);
		lcd_putpixel(156, 126, BLACK);
		lcd_putpixel(157, 126, BLACK);
		lcd_putpixel(156, 127, BLACK);
		lcd_putpixel(157, 127, BLACK);
		lcd_putint_x2(138,102, BLACK, metros);
		if(decimales < 10){
			lcd_putint_x2(158,102, BLACK, 0);
			lcd_putint_x2(174,102, BLACK, decimales);
		}
		else{
			lcd_putint_x2(158,102, BLACK, decimales);
		}

		lcd_puts(250,115, BLACK, "EN M");
		lcd_draw_box(202,100, 232, 130, BLACK, 3); //+
		lcd_putchar(214, 110, BLACK, '>');
		lcd_draw_box(96,100, 126, 130, BLACK, 3); //-
		lcd_putchar(108, 110, BLACK, '<');
		lcd_draw_box(130, 180, 190, 210, BLACK, 3); //ok
		lcd_puts(152, 188, BLACK, "OK");
		lcd_draw_box(130, 30, 190, 60, BLACK, 3); //salir
		lcd_puts(142, 40, BLACK, "SALIR");

		uint16 x1, y1;
		ts_getpos(&x1, &y1);
		if(x1 >= 202 && x1 <= 232 && y1 >= 100 && y1 <= 130){ //+
			if(decimales < 51 || metros < 2){
				distanciaPasos = FADD(distanciaPasos, TOFIX(ufix32, 0.01, Q));
			}
		}
		else if (x1 >= 96 && x1 <= 126 && y1 >= 100 && y1 <= 130){ //-
			if(decimales > 10 || metros > 0){
				distanciaPasos = FSUB(distanciaPasos, TOFIX(ufix32, 0.01, Q));
			}
		}
		else if (x1 >= 130 && x1 <= 190 && y1 >= 180 && y1 <= 210){ //ok
			*distanciaPaso = distanciaPasos;
			lcd_clear();
			break;
		}
		else if(x1 >= 130 && x1 <= 190 && y1 >= 30 && y1 <= 60){ // salir
			lcd_clear();
			break;
		}
	}
}

rtc_time_t menuAlarma ( void )
{
	lcd_clear();
	uint8 h1=0,h2=0,m1=0,m2=0,s1=0,s2=0;
	rtc_time_t a;

	a.hour = 0; a.min = 0; a.sec = 0;
	while(1){
		uint16 x1, y1;
		lcd_putint_x2(100, 102, BLACK, h1);
		lcd_putint_x2(116, 102, BLACK, h2);
		lcd_putchar_x2(129, 102, BLACK, ':');
		lcd_putint_x2(145, 102, BLACK, m1);
		lcd_putint_x2(161, 102, BLACK, m2);
		lcd_putchar_x2(174, 102, BLACK, ':');
		lcd_putint_x2(190, 102, BLACK, s1);
		lcd_putint_x2(206, 102, BLACK, s2);

		lcd_draw_box(99,70, 129, 100, BLACK, 3); //mas h
		lcd_putchar(110,80 ,BLACK, '+');
		lcd_draw_box(99 ,133, 129, 163, BLACK, 3); //menos h
		lcd_putchar(110,145 ,BLACK, '-');

		lcd_draw_box(144,70, 174, 100, BLACK, 3); //mas m
		lcd_putchar(155,80 ,BLACK, '+');
		lcd_draw_box(144 ,133, 174, 163, BLACK, 3); //menos m
		lcd_putchar(155,145 ,BLACK, '-');

		lcd_draw_box(189 ,70, 219, 100, BLACK, 3); //mas s
		lcd_putchar(200,80 ,BLACK, '+');
		lcd_draw_box(189 ,133, 219, 163, BLACK, 3); //menos s
		lcd_putchar(200,145 ,BLACK, '-');

		lcd_draw_box(130, 180, 190, 210, BLACK, 3); //ok
		lcd_puts(152, 188, BLACK, "OK");

		lcd_draw_box(130, 20, 190, 50, BLACK, 3); //salir
		lcd_puts(142, 30, BLACK, "SALIR");

		ts_getpos(&x1, &y1);
		if(x1 >= 130 && x1 <= 190 && y1 >= 180 && y1 <= 210){ //ok
			lcd_clear();
			uint8 h = h1 * 10 + h2;
			uint8 m = m1 * 10 + m2;
			uint8 s = s1 * 10 + s2;
			a.hour = h; a.min = m; a.sec = s;
			flagAlarma = TRUE;

			break;
		}
		else if(x1 >= 130 && x1 <= 190 && y1 >= 20 && y1 <= 50){//salir
			//flagAlarma = FALSE;
			lcd_clear();
			break;
		}
		else if(x1 >= 99 && x1 <= 129 && y1 >= 70 && y1 <= 100){ //h+
			if(h1 == 2 && h2 == 4){
				h1 = 0;
				h2 = 0;
			}
			else if(h2 == 9){
				h2 = 0;
				h1++;
			}
			else{
				h2++;
			}
			lcd_putint_x2(100, 102, BLACK, h1);
			lcd_putint_x2(116, 102, BLACK, h2);
		}
		else if(x1 >= 99 && x1 <= 129 && y1 >= 133 && y1 <= 163){ //h-
			if(h1 == 0 && h2 == 0){
				h1 = 2;
				h2 = 4;
			}
			else if(h2==0){
				h2 = 9;
				h1--;
			}
			else{
				h2--;
			}
			lcd_putint_x2(100, 102, BLACK, h1);
			lcd_putint_x2(116, 102, BLACK, h2);
			}
			else if(x1 >= 144 && x1 <= 174 && y1 >= 70 && y1 <= 100){ //m+
				if(m1 == 5 && m2 == 9){
					m1 = 0;
					m2 = 0;
				}
				else if(m2 == 9){
					m2 = 0;
					m1++;
				}
				else{
					m2++;
				}
			lcd_putint_x2(145, 102, BLACK, m1);
			lcd_putint_x2(161, 102, BLACK, m2);
			}
			else if(x1 >= 144 && x1 <= 174 && y1 >= 133 && y1 <= 163){ //m-
				if(m1 == 0 && m2 == 0){
					m1 = 5;
					m2 = 9;
				}
				else if(m2==0){
					m2 = 9;
					m1--;
				}
				else{
					m2--;
				}
				lcd_putint_x2(145, 102, BLACK, m1);
				lcd_putint_x2(161, 102, BLACK, m2);
			}
			else if(x1 >= 189 && x1 <= 219 && y1 >= 70 && y1 <= 100){ //s+
				if(s1 == 5 && s2 == 9){
					s1 = 0;
					s2 = 0;
				}
				else if(s2 == 9){
					s2 = 0;
					s1++;
				}
				else{
					s2++;
				}
				lcd_putint_x2(190, 102, BLACK, s1);
				lcd_putint_x2(206, 102, BLACK, s2);
			}
			else if(x1 >= 189 && x1 <= 219 && y1 >= 133 && y1 <= 163){ //s-
				if(s1 == 0 && s2 == 0){
					s1 = 5;
					s2 = 9;
				}
				else if(s2==0){
					s2 = 9;
					s1--;
				}
				else{
					s2--;
				}
				lcd_putint_x2(190, 102, BLACK, s1);
				lcd_putint_x2(206, 102, BLACK, s2);
			}

	}

	return a;
}

void pinta_alarma( rtc_time_t alarma )
{
	if(flagAlarma){
			lcd_puts(260, 156, BLACK, "ON");
			if(alarma.hour < 10){
				lcd_putint(210, 180, BLACK, 0);
				lcd_putint(218, 180, BLACK, alarma.hour);
			}
			else{
				lcd_putint(210, 180, BLACK, alarma.hour);
			}

			lcd_putchar(225, 180, BLACK, ':');
			if(alarma.min < 10){
				lcd_putint(232, 180, BLACK, 0);
				lcd_putint(240, 180, BLACK, alarma.min);
			}
			else{
				lcd_putint(232, 180, BLACK, alarma.min);
			}
			lcd_putchar(247, 180, BLACK, ':');
			if(alarma.sec < 10){
				lcd_putint(254, 180, BLACK, 0);
				lcd_putint(263, 180, BLACK, alarma.sec);
			}
			else{
				lcd_putint(254, 180, BLACK, alarma.sec);
			}
		}
		else{
			lcd_puts(260, 156, BLACK, "OFF");
		}
}

void readBeats ( uint16 *beatsPerMinute )
{
	lcd_puts(10, 20, BLACK, "Introduce las pulsaciones por minuto: ");
	//xleft yup xright ydown
	lcd_draw_box(40 ,90, 100, 150, BLACK, 3); //menos
	lcd_draw_hline(50, 90, 120, BLACK, 3);

	lcd_draw_box(230 ,90, 290, 150, BLACK, 3); //mas
	lcd_draw_hline(240, 280 , 120, BLACK, 3);
	lcd_draw_vline(100, 140, 260, BLACK, 3);

	lcd_draw_box(140, 160, 190, 210, BLACK, 3); //ok
	lcd_puts(157, 178, BLACK, "OK");

	lcd_putint_x2(150, 110, BLACK ,*beatsPerMinute);

	uint16 x, y;

	while(1){
		ts_getpos(&x, &y);
		if(x >= 140 && x <= 190 && y >= 160 && y <= 210){ //ok
			break;
		}
		if(x >= 40 && x <= 100 && y >= 90 && y <= 150){ //menos
			(*beatsPerMinute)--;
			lcd_putint_x2(150, 110, BLACK ,*beatsPerMinute);
		}
		if(x >= 230 && x <= 290 && y >= 90  && y <= 150){ //mas
			(*beatsPerMinute)++;
			lcd_putint_x2(150, 110, BLACK ,*beatsPerMinute);
		}
	}
	lcd_clear();

}

void readSteps ( uint16 *stepsPerMinute )
{
	uint16 x, y;
	lcd_puts(10, 20, BLACK, "Introduce los pasos por minuto: ");
		//xleft yup xright ydown
	lcd_draw_box(40 ,90, 100, 150, BLACK, 3); //menos
	lcd_draw_hline(50, 90, 120, BLACK, 3);

	lcd_draw_box(230 ,90, 290, 150, BLACK, 3); //mas
	lcd_draw_hline(240, 280 , 120, BLACK, 3);
	lcd_draw_vline(100, 140, 260, BLACK, 3);

	lcd_draw_box(140, 160, 190, 210, BLACK, 3); //ok
	lcd_puts(157, 178, BLACK, "OK");

	lcd_putint_x2(150, 110, BLACK ,*stepsPerMinute);


	while(1){
		ts_getpos(&x, &y);
		if(x >= 140 && x <= 190 && y >= 160 && y <= 210){ //ok
			break;
		}
		if(x >= 40 && x <= 100 && y >= 90 && y <= 150){ //menos
			(*stepsPerMinute)--;
			lcd_putint_x2(150, 110, BLACK ,*stepsPerMinute);
		}
		if(x >= 230 && x <= 290 && y >= 90  && y <= 150){ //mas
			(*stepsPerMinute)++;
			lcd_putint_x2(150, 110, BLACK ,*stepsPerMinute);
		}
	}
	lcd_clear();
}

void alertaPulsaciones ( uint16 beatsPerMinute )
{
	lcd_clear();
	lcd_puts(10, 10, BLACK, "Cuidado, pulsaciones muy elevadas!");
	lcd_puts(10, 50, BLACK, "Actualmente son de mas de 200!");
	lcd_puts(10, 90, BLACK, "Relajate y toma un descanso, por favor");
	uda1341ts_setvol( VOL_MED );
	iis_playWawFile( DTMF0, TRUE );
	sw_delay_s(5);
	iis_pause();
	lcd_clear();
}


void beatHandler( void )
{
	if(!flagUpdateSB)
		nBeatsIn10Secs++;
    newBeat = TRUE;
    I_ISPC  = BIT_BEATEMULATOR;
}

void stepHandler( void )
{
	if(!flagUpdateSB)
		nStepsIn10Secs++;
    newStep = TRUE;
    I_ISPC  = BIT_STEPEMULATOR;
}

void isr_tick( void ){ //1000 ticks -> 1 sec
	static uint16 cont1000ticks    = 1000;
	static uint16 cont500ticks    = 500;
	static uint16 cont100ticks  = 100;
	if( !(--cont100ticks) )
	{
		cont100ticks = 100;
		flagRTC = TRUE;
	}
	if( !(--cont500ticks) ){
		cont500ticks = 500;
		flagCheckPulsaciones = TRUE;
	}

	if( !(--cont1000ticks) )
	{
	    cont1000ticks = 1000;
	    flagUpdateSB = TRUE;
	}

	I_ISPC = BIT_TIMER0;
}

void activaAlertaAlarma ( rtc_time_t alarma )
{
	flagPBS = FALSE;
	lcd_clear();
	lcd_puts_x2(10, 20, BLACK, "�Alarma a las: ");
	put_time_x2(alarma);
	lcd_puts(30, 200, BLACK, "Pulsa el PBS para finalizar.");
	uda1341ts_setvol( VOL_MED );
	iis_playWawFile( NOKIATUNE, TRUE );
	while(!flagPBS);
	flagPBS = FALSE;
	iis_pause();
	lcd_clear();
}

void separateParts(ufix32 f, uint32* parte_entera, uint32* parte_decimal) {
	//f= FADD(f, TOFIX(ufix32, 0.0151, Q));
    *parte_entera = f >> Q;
    *parte_decimal = ((f & ((1 << Q) - 1)) * 100) >> Q;  // Obtener los q bits de parte fraccionaria y convertir a decimal
}

boolean checkAlarm ( rtc_time_t rtc_time, rtc_time_t alarm ){
	if((rtc_time.hour == alarm.hour) &&(rtc_time.min == alarm.min) && (rtc_time.sec == alarm.sec)){
		return TRUE;
	}
	return FALSE;
}

void pintaDistancia ( ufix32 distanciaAndada )
{
	uint32 parte_entera, parte_decimal;
	separateParts(distanciaAndada, &parte_entera, &parte_decimal);
	/*uint32 parteEntera = distanciaAndada / 100;
	uint8 parteDecimal = distanciaAndada % 100;*/
	lcd_puts(125, 10, BLACK, "Distancia (m): ");
	lcd_putint(240, 10, BLACK, parte_entera );
	lcd_putchar(270, 10, BLACK, '.');
	if(parte_decimal > 10){
		lcd_putint(280, 10, BLACK, parte_decimal);
	}
	else{
		lcd_putint(280, 10, BLACK, 0);
		lcd_putint(288, 10, BLACK, parte_decimal);
	}
}

void isr_ts( void )
{
	flagTS = TRUE;
	I_ISPC = BIT_TS;
}
void isr_pbs( void )
{
	flagPBS = TRUE;
    EXTINTPND = BIT_RIGHTPB;
    EXTINTPND = BIT_LEFTPB;
    I_ISPC = BIT_PB;
}
