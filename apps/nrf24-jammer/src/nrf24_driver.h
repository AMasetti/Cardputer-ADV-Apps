// Driver minimalista nRF24L01+ para ESP32
// Sin dependencias externas, usa SPI.h nativo

#pragma once
#include <SPI.h>

// Comandos
#define NRF24_R_REGISTER    0x00
#define NRF24_W_REGISTER    0x20
#define NRF24_R_RX_PAYLOAD  0x61
#define NRF24_W_TX_PAYLOAD  0xA0
#define NRF24_FLUSH_TX      0xE1
#define NRF24_FLUSH_RX      0xE2
#define NRF24_REUSE_TX_PL   0xE3
#define NRF24_NOP           0xFF

// Registros
#define NRF24_REG_CONFIG      0x00
#define NRF24_REG_EN_AA       0x01
#define NRF24_REG_EN_RXADDR   0x02
#define NRF24_REG_SETUP_AW    0x03
#define NRF24_REG_SETUP_RETR  0x04
#define NRF24_REG_RF_CH       0x05
#define NRF24_REG_RF_SETUP    0x06
#define NRF24_REG_STATUS      0x07
#define NRF24_REG_OBSERVE_TX  0x08
#define NRF24_REG_CD          0x09
#define NRF24_REG_RX_ADDR_P0  0x0A
#define NRF24_REG_RX_ADDR_P1  0x0B
#define NRF24_REG_TX_ADDR     0x10
#define NRF24_REG_RX_PW_P0    0x11
#define NRF24_REG_FIFO_STATUS 0x17
#define NRF24_REG_DYNPD       0x1C
#define NRF24_REG_FEATURE     0x1D

class NRF24Jammer {
public:
    uint8_t cePin;
    uint8_t csnPin;
    bool initialized;

    NRF24Jammer(uint8_t ce, uint8_t csn) : cePin(ce), csnPin(csn), initialized(false) {}

    bool begin() {
        pinMode(cePin, OUTPUT);
        pinMode(csnPin, OUTPUT);
        digitalWrite(cePin, LOW);
        digitalWrite(csnPin, HIGH);
        
        SPI.begin(40, 39, 14, csnPin);
        
        delay(100);
        
        // Verificar que responde leyendo CONFIG
        uint8_t cfg = readReg(NRF24_REG_CONFIG);
        if (cfg == 0xFF) return false; // No conectado
        
        // Configurar para jamming
        writeReg(NRF24_REG_CONFIG, 0x0E);      // PWR_UP, PTX, CRC off
        writeReg(NRF24_REG_EN_AA, 0x00);       // Auto-ACK off
        writeReg(NRF24_REG_EN_RXADDR, 0x00);   // No RX pipes
        writeReg(NRF24_REG_SETUP_AW, 0x03);    // 5 bytes address
        writeReg(NRF24_REG_SETUP_RETR, 0x00);  // No retransmission
        writeReg(NRF24_REG_RF_SETUP, 0x0E);    // 2Mbps, 0dBm
        writeReg(NRF24_REG_STATUS, 0x70);      // Clear interrupts
        writeReg(NRF24_REG_DYNPD, 0x00);       // No dynamic payload
        writeReg(NRF24_REG_FEATURE, 0x00);     // No features
        
        // Address
        uint8_t addr[5] = {0xE7, 0xE7, 0xE7, 0xE7, 0xE7};
        writeRegMulti(NRF24_REG_TX_ADDR, addr, 5);
        
        flushTx();
        
        initialized = true;
        return true;
    }

    void setChannel(uint8_t ch) {
        writeReg(NRF24_REG_RF_CH, ch & 0x7F);
    }

    void send(const uint8_t* data, uint8_t len) {
        digitalWrite(csnPin, LOW);
        SPI.transfer(NRF24_W_TX_PAYLOAD);
        for (uint8_t i = 0; i < len; i++) {
            SPI.transfer(data[i]);
        }
        digitalWrite(csnPin, HIGH);
        
        // Pulse CE para transmitir
        digitalWrite(cePin, HIGH);
        delayMicroseconds(15);
        digitalWrite(cePin, LOW);
    }

    bool isConnected() {
        if (!initialized) return false;
        uint8_t cfg = readReg(NRF24_REG_CONFIG);
        return cfg != 0xFF;
    }

private:
    uint8_t readReg(uint8_t reg) {
        digitalWrite(csnPin, LOW);
        SPI.transfer(NRF24_R_REGISTER | reg);
        uint8_t val = SPI.transfer(NRF24_NOP);
        digitalWrite(csnPin, HIGH);
        return val;
    }

    void writeReg(uint8_t reg, uint8_t val) {
        digitalWrite(csnPin, LOW);
        SPI.transfer(NRF24_W_REGISTER | reg);
        SPI.transfer(val);
        digitalWrite(csnPin, HIGH);
    }

    void writeRegMulti(uint8_t reg, const uint8_t* data, uint8_t len) {
        digitalWrite(csnPin, LOW);
        SPI.transfer(NRF24_W_REGISTER | reg);
        for (uint8_t i = 0; i < len; i++) {
            SPI.transfer(data[i]);
        }
        digitalWrite(csnPin, HIGH);
    }

    void flushTx() {
        digitalWrite(csnPin, LOW);
        SPI.transfer(NRF24_FLUSH_TX);
        digitalWrite(csnPin, HIGH);
    }
};
