#ifndef PIN_CONFIG_H
#define PIN_CONFIG_H

// $[LETIMER0]
// [LETIMER0]$

// $[ADC0]
// [ADC0]$

// $[ACMP0]
// [ACMP0]$

// $[ACMP1]
// [ACMP1]$

// $[PCNT0]
// [PCNT0]$

// $[I2C0]
// [I2C0]$

// $[EUSART0]
// EUSART0 RX on PA00
#ifndef EUSART0_RX_PORT                         
#define EUSART0_RX_PORT                          SL_GPIO_PORT_A
#endif
#ifndef EUSART0_RX_PIN                          
#define EUSART0_RX_PIN                           0
#endif

// EUSART0 TX on PB02
#ifndef EUSART0_TX_PORT                         
#define EUSART0_TX_PORT                          SL_GPIO_PORT_B
#endif
#ifndef EUSART0_TX_PIN                          
#define EUSART0_TX_PIN                           2
#endif

// [EUSART0]$

// $[CMU]
// [CMU]$

// $[LFXO]
// [LFXO]$

// $[PRS.ASYNCH0]
// [PRS.ASYNCH0]$

// $[PRS.ASYNCH1]
// [PRS.ASYNCH1]$

// $[PRS.ASYNCH2]
// [PRS.ASYNCH2]$

// $[PRS.ASYNCH3]
// [PRS.ASYNCH3]$

// $[PRS.ASYNCH4]
// [PRS.ASYNCH4]$

// $[PRS.ASYNCH5]
// [PRS.ASYNCH5]$

// $[PRS.ASYNCH6]
// [PRS.ASYNCH6]$

// $[PRS.ASYNCH7]
// [PRS.ASYNCH7]$

// $[PRS.ASYNCH8]
// [PRS.ASYNCH8]$

// $[PRS.ASYNCH9]
// [PRS.ASYNCH9]$

// $[PRS.ASYNCH10]
// [PRS.ASYNCH10]$

// $[PRS.ASYNCH11]
// [PRS.ASYNCH11]$

// $[PRS.SYNCH0]
// [PRS.SYNCH0]$

// $[PRS.SYNCH1]
// [PRS.SYNCH1]$

// $[PRS.SYNCH2]
// [PRS.SYNCH2]$

// $[PRS.SYNCH3]
// [PRS.SYNCH3]$

// $[GPIO]
// [GPIO]$

// $[TIMER0]
// TIMER0 CC0 on PD01
#ifndef TIMER0_CC0_PORT                         
#define TIMER0_CC0_PORT                          SL_GPIO_PORT_D
#endif
#ifndef TIMER0_CC0_PIN                          
#define TIMER0_CC0_PIN                           1
#endif

// [TIMER0]$

// $[TIMER1]
// [TIMER1]$

// $[TIMER2]
// TIMER2 CC0 on PA03
#ifndef TIMER2_CC0_PORT                         
#define TIMER2_CC0_PORT                          SL_GPIO_PORT_A
#endif
#ifndef TIMER2_CC0_PIN                          
#define TIMER2_CC0_PIN                           3
#endif

// TIMER2 CC1 on PA04
#ifndef TIMER2_CC1_PORT                         
#define TIMER2_CC1_PORT                          SL_GPIO_PORT_A
#endif
#ifndef TIMER2_CC1_PIN                          
#define TIMER2_CC1_PIN                           4
#endif

// TIMER2 CC2 on PB00
#ifndef TIMER2_CC2_PORT                         
#define TIMER2_CC2_PORT                          SL_GPIO_PORT_B
#endif
#ifndef TIMER2_CC2_PIN                          
#define TIMER2_CC2_PIN                           0
#endif

// [TIMER2]$

// $[TIMER3]
// [TIMER3]$

// $[ETAMPDET]
// [ETAMPDET]$

// $[PIXELRZ0]
// [PIXELRZ0]$

// $[I2C1]
// [I2C1]$

// $[LEDDRV0]
// [LEDDRV0]$

// $[EUSART1]
// EUSART1 RX on PC02
#ifndef EUSART1_RX_PORT                         
#define EUSART1_RX_PORT                          SL_GPIO_PORT_C
#endif
#ifndef EUSART1_RX_PIN                          
#define EUSART1_RX_PIN                           2
#endif

// EUSART1 SCLK on PC01
#ifndef EUSART1_SCLK_PORT                       
#define EUSART1_SCLK_PORT                        SL_GPIO_PORT_C
#endif
#ifndef EUSART1_SCLK_PIN                        
#define EUSART1_SCLK_PIN                         1
#endif

// EUSART1 TX on PD00
#ifndef EUSART1_TX_PORT                         
#define EUSART1_TX_PORT                          SL_GPIO_PORT_D
#endif
#ifndef EUSART1_TX_PIN                          
#define EUSART1_TX_PIN                           0
#endif

// [EUSART1]$

// $[EUSART2]
// [EUSART2]$

// $[PIXELRZ1]
// [PIXELRZ1]$

// $[I2C2]
// [I2C2]$

// $[PTI]
// [PTI]$

// $[MODEM]
// [MODEM]$

// $[CUSTOM_PIN_NAME]
#ifndef _PORT                                   
#define _PORT                                    SL_GPIO_PORT_A
#endif
#ifndef _PIN                                    
#define _PIN                                     0
#endif

















// [CUSTOM_PIN_NAME]$


#endif // PIN_CONFIG_H


