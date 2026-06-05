#include "spidrv.h"
#include "sl_spidrv_instances.h"
#include "sl_assert.h"


#include "sl_spidrv_eusart_SPI_SM15135E_config.h"
SPIDRV_HandleData_t sl_spidrv_eusart_SPI_SM15135E_handle_data;
SPIDRV_Handle_t sl_spidrv_eusart_SPI_SM15135E_handle = &sl_spidrv_eusart_SPI_SM15135E_handle_data;

SPIDRV_Init_t sl_spidrv_eusart_init_SPI_SM15135E = {
  .port = SL_SPIDRV_EUSART_SPI_SM15135E_PERIPHERAL,
  .portTx = SL_SPIDRV_EUSART_SPI_SM15135E_TX_PORT,
  .portRx = SL_SPIDRV_EUSART_SPI_SM15135E_RX_PORT,
  .portClk = SL_SPIDRV_EUSART_SPI_SM15135E_SCLK_PORT,
#if defined(SL_SPIDRV_EUSART_SPI_SM15135E_CS_PORT)
  .portCs = SL_SPIDRV_EUSART_SPI_SM15135E_CS_PORT,
#endif
  .pinTx = SL_SPIDRV_EUSART_SPI_SM15135E_TX_PIN,
  .pinRx = SL_SPIDRV_EUSART_SPI_SM15135E_RX_PIN,
  .pinClk = SL_SPIDRV_EUSART_SPI_SM15135E_SCLK_PIN,
#if defined(SL_SPIDRV_EUSART_SPI_SM15135E_CS_PIN)
  .pinCs = SL_SPIDRV_EUSART_SPI_SM15135E_CS_PIN,
#endif
  .bitRate = SL_SPIDRV_EUSART_SPI_SM15135E_BITRATE,
  .frameLength = SL_SPIDRV_EUSART_SPI_SM15135E_FRAME_LENGTH,
  .dummyTxValue = 0,
  .type = SL_SPIDRV_EUSART_SPI_SM15135E_TYPE,
  .bitOrder = SL_SPIDRV_EUSART_SPI_SM15135E_BIT_ORDER,
  .clockMode = SL_SPIDRV_EUSART_SPI_SM15135E_CLOCK_MODE,
  .csControl = SL_SPIDRV_EUSART_SPI_SM15135E_CS_CONTROL,
  .slaveStartMode = SL_SPIDRV_EUSART_SPI_SM15135E_SLAVE_START_MODE,
};

void sl_spidrv_init_instances(void) {
#if !defined(SL_SPIDRV_EUSART_SPI_SM15135E_CS_PIN)
  EFM_ASSERT(sl_spidrv_eusart_init_SPI_SM15135E.csControl == spidrvCsControlApplication);
#endif 
  SPIDRV_Init(sl_spidrv_eusart_SPI_SM15135E_handle, &sl_spidrv_eusart_init_SPI_SM15135E);
}
