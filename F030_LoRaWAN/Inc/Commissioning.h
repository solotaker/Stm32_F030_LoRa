/*
 / _____)             _              | |
( (____  _____ ____ _| |_ _____  ____| |__
 \____ \| ___ |    (_   _) ___ |/ ___)  _ \
 _____) ) ____| | | || |_| ____( (___| | | |
(______/|_____)_|_|_| \__)_____)\____)_| |_|
    (C)2015 Semtech

Description: End device commissioning parameters

License: Revised BSD License, see LICENSE.TXT file include in the project

Maintainer: Miguel Luis and Gregory Cristian
*/
#ifndef __LORA_COMMISSIONING_H__
#define __LORA_COMMISSIONING_H__

/*!
 * When set to 1 the application uses the Over-the-Air activation procedure
 * When set to 0 the application uses the Personalization activation procedure
 */
#define OVER_THE_AIR_ACTIVATION                     1

/*!
 * Indicates if the end-device is to be connected to a private or public network
 */
#define LORAWAN_PUBLIC_NETWORK                      true

/*!
 * IEEE Organizationally Unique Identifier ( OUI ) (big endian)
 * \remark This is unique to a company or organization
 */
#define IEEE_OUI                                    0x22, 0x32, 0x33

/*!
 * Mote device IEEE EUI (big endian)
 *
 * \remark In this application the value is automatically generated by calling
 *         BoardGetUniqueId function
 */
#define LORAWAN_DEVICE_EUI                          { IEEE_OUI, 0xaa, 0x66, 0xac, 0x66, 0x67 }

/*!
 * Application IEEE EUI (big endian)
 */
#define LORAWAN_APPLICATION_EUI                     { 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x04 }

/*!
 * AES encryption/decryption cipher application key
 */
#define LORAWAN_APPLICATION_KEY                     { 0xa8, 0x28, 0x38, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88 }

/*!
 * Current network ID
 */
#define LORAWAN_NETWORK_ID                          ( uint32_t )0x00666888

/*!
 * Device address on the network (big endian)
 *
 * \remark In this application the value is automatically generated using
 *         a pseudo random generator seeded with a value derived from
 *         BoardUniqueId value if LORAWAN_DEVICE_ADDRESS is set to 0
 */
#define LORAWAN_DEVICE_ADDRESS                      ( uint32_t )0x66666666

/*!
 * AES encryption/decryption cipher network session key
 */
#define LORAWAN_NWKSKEY                             { 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88 }

/*!
 * AES encryption/decryption cipher application session key
 */
#define LORAWAN_APPSKEY                             { 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88, 0x88 }

#endif // __LORA_COMMISSIONING_H__
