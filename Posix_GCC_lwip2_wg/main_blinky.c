/*
 * FreeRTOS V202112.00
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
 * FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 *
 */

/******************************************************************************
 * NOTE 1: The FreeRTOS demo threads will not be running continuously, so
 * do not expect to get real time behaviour from the FreeRTOS Linux port, or
 * this demo application.  Also, the timing information in the FreeRTOS+Trace
 * logs have no meaningful units.  See the documentation page for the Linux
 * port for further information:
 * https://freertos.org/FreeRTOS-simulator-for-Linux.html
 *
 * NOTE 2:  This project provides two demo applications.  A simple blinky style
 * project, and a more comprehensive test and demo application.  The
 * mainCREATE_SIMPLE_BLINKY_DEMO_ONLY setting in main.c is used to select
 * between the two.  See the notes on using mainCREATE_SIMPLE_BLINKY_DEMO_ONLY
 * in main.c.  This file implements the simply blinky version.  Console output
 * is used in place of the normal LED toggling.
 *
 * NOTE 3:  This file only contains the source code that is specific to the
 * basic demo.  Generic functions, such FreeRTOS hook functions, are defined
 * in main.c.
 ******************************************************************************
 *
 * main_blinky() creates one queue, one software timer, and two tasks.  It then
 * starts the scheduler.
 *
 * Questo esempio contiene: ======================= TRADUZIONE PER GIOVANI INESPERTI ============================================
 * - prvQueueSendTimerCallback() callback del timer (ripetitivo 2 sec, registrato nel main), manda dati ("200") sulla coda xQueue
 * - prvQueueSendTask()          task che invia dati ("100") ogni 200 ms sulla coda xQueue
 * - prvQueueReceiveTask()       task che riceve dati dalla coda xQueue (per cui riceve dati dal SendTask e dalla TimerCallback) (stampa su printf)
 * OSS non attendersi che i timer siano accurati, leggi note
 * OSS non usare syscall tipo printf più di così se no si sfascia tutto (in teoria dovresti comunicare solo con freertos)
 *
 * The Queue Send Task:
 * The queue send task is implemented by the prvQueueSendTask() function in
 * this file.  It uses vTaskDelayUntil() to create a periodic task that sends
 * the value 100 to the queue every 200 milliseconds (please read the notes
 * above regarding the accuracy of timing under Linux).
 *
 * The Queue Send Software Timer:
 * The timer is an auto-reload timer with a period of two seconds.  The timer's
 * callback function writes the value 200 to the queue.  The callback function
 * is implemented by prvQueueSendTimerCallback() within this file.
 *
 * The Queue Receive Task:
 * The queue receive task is implemented by the prvQueueReceiveTask() function
 * in this file.  prvQueueReceiveTask() waits for data to arrive on the queue.
 * When data is received, the task checks the value of the data, then outputs a
 * message to indicate if the data came from the queue send task or the queue
 * send software timer.
 *
 * Expected Behaviour:
 * - The queue send task writes to the queue every 200ms, so every 200ms the
 *   queue receive task will output a message indicating that data was received
 *   on the queue from the queue send task.
 * - The queue send software timer has a period of two seconds, and is reset
 *   each time a key is pressed.  So if two seconds expire without a key being
 *   pressed then the queue receive task will output a message indicating that
 *   data was received on the queue from the queue send software timer.
 *
 * NOTE:  Console input and output relies on Linux system calls, which can
 * interfere with the execution of the FreeRTOS Linux port. This demo only
 * uses Linux system call occasionally. Heavier use of Linux system calls
 * may crash the port.
 */

#include <stdio.h>
#include <pthread.h>

// Kernel includes.
#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"
#include "semphr.h"

// LwIP includes
#include "lwip/sockets.h"
#include "lwip/tcpip.h"
#include "lwip/ip.h"
#include "netif/ethernet.h"
#include "netif/tapif.h"

// WireGuard VPN module
#include "wireguardif.h"

// Local includes.
#include "console.h"

/* Priorities at which the tasks are created. */
#define mainQUEUE_RECEIVE_TASK_PRIORITY    ( tskIDLE_PRIORITY + 2 )
#define mainQUEUE_SEND_TASK_PRIORITY       ( tskIDLE_PRIORITY + 1 )

/* The rate at which data is sent to the queue.  The times are converted from
 * milliseconds to ticks using the pdMS_TO_TICKS() macro. */
#define mainTASK_SEND_FREQUENCY_MS         pdMS_TO_TICKS( 200UL )
#define mainTIMER_SEND_FREQUENCY_MS        pdMS_TO_TICKS( 2000UL )

/* The number of items the queue can hold at once. */
#define mainQUEUE_LENGTH                   ( 2 )

/* The values sent to the queue receive task from the queue send task and the
 * queue send software timer respectively. */
#define mainVALUE_SENT_FROM_TASK           ( 100UL )
#define mainVALUE_SENT_FROM_TIMER          ( 200UL )

/*-----------------------------------------------------------*/

/*
 * The tasks as described in the comments at the top of this file.
 */
static void prvQueueReceiveTask( void * pvParameters );
static void prvQueueSendTask( void * pvParameters );

/*
 * The callback function executed when the software timer expires.
 */
static void prvQueueSendTimerCallback( TimerHandle_t xTimerHandle );

/*-----------------------------------------------------------*/

/* The queue used by both tasks. */
static QueueHandle_t xQueue = NULL;

/* A software timer that is started from the tick hook. */
static TimerHandle_t xTimer = NULL;

/*-----------------------------------------------------------*/

// TCP Socket declarations
// Preallocate tcp data buffer
static char payload[256] = "(default message...)";
static int sock = 0;
static char sock_isready = 0; // TODO dovrebbe diventare un segnale o semaforo anche se non è necessario (non c'è rischio scritture concorrenti, basta che blocchi le scritture TCP)
char rx_buffer[128];
char errmsg[256] = "";

// TCP Socket info
//static const char host_ip[] = "192.168.2.1";
static const char host_ip[] = "192.168.77.1";
//static const char host_ip[] = CONFIG_EXAMPLE_PING_ADDRESS;
static const unsigned short PORT = 3333;

// Declarations necessary for WireGuard
static struct netif wg_netif_struct = {0};
static struct netif *wg_netif = NULL;
static struct wireguardif_peer peer = {0};
static uint8_t wireguard_peer_index = WIREGUARDIF_INVALID_INDEX;

// Wireguard tunnel info
char private_key[] = "qEnqWM2tORDVhvTZeGlK+c5jGxiFeVBRLiFPx8iAtXg="; // of the local peer
unsigned short listen_port = 51820;       // of the local peer
char allowed_ip[] = "192.168.77.2";       // local peer address inside the vpn
char allowed_ip_mask[] = "255.255.255.0"; // vpn subnet mask
unsigned short port = 51820;
unsigned short persistent_keepalive = 30; // [sec] send at least an empty packet this often (to keep UDP route open).

char endpoint[] = "192.168.2.1";          // Public address of the remote peer
char public_key[] = "dk0lTzfwjmWHtggRkw8rmUkQQqox26G8QSBAdXebAwQ=";  // of the remote peer
ip_addr_t rpeer_allowed_ip = IPADDR4_INIT_BYTES(0, 0, 0, 0);         // Allow any IP for the remote peer (inside the VPN): 0.0.0.0/0
ip_addr_t rpeer_allowed_mask = IPADDR4_INIT_BYTES(0, 0, 0, 0);       // (equivalent to \0)

/// Connect to wireguard peer
static int unix_wireguard_setup(/*wireguard_ctx_t* ctx, char* dest_peer_address*/)
{
	int err = 0;

    struct wireguardif_init_data wg = {0};
    wg.private_key = private_key;
    wg.listen_port = listen_port;
    wg.bind_netif = NULL;

    ip_addr_t ip_addr;
    ip_addr_t netmask;
    ip_addr_t gateway = IPADDR4_INIT_BYTES(0, 0, 0, 0);
    //ip_addr_t gateway = IPADDR4_INIT_BYTES(192, 168, 77, 1);
    ipaddr_aton(allowed_ip, &ip_addr);
    ipaddr_aton(allowed_ip_mask, &netmask);

	//err = esp_wireguard_init(&wg_config, ctx); // controllava livello di entropia

	//err = esp_wireguard_netif_create(ctx->config);
	//if (err != ESP_OK) {
	//	sprintf(errmsg, "esp_wireguard_netif_create error: %d\n", err);
	//	console_print(errmsg);
	//	goto fail;
	//}
    wg_netif = netif_add(
            &wg_netif_struct,
            ip_2_ip4(&ip_addr),
            ip_2_ip4(&netmask),
            ip_2_ip4(&gateway),
            &wg, &wireguardif_init,
            &ip_input);
    if (wg_netif == NULL) {
		console_print("[wg] netif_add failed.\n");
		err = -7;
        goto fail;
    }

    /* Mark the interface as administratively up, link up flag is set
     * automatically when peer connects */
    netif_set_up(wg_netif);

	/* Initialize the first WireGuard peer structure */
//	err = esp_wireguard_peer_init(ctx->config, &peer);
//	if (err != ESP_OK) {
//		sprintf(errmsg, "esp_wireguard_peer_init error: %d\n", err);
//		console_print(errmsg);
//		goto fail;
//	}
	// Set up data of the remote peer
	peer.allowed_ip = rpeer_allowed_ip;
	peer.allowed_mask = rpeer_allowed_mask;
	peer.public_key = public_key;
	peer.preshared_key = NULL; //""; //(uint8_t*)preshared_key;
	peer.keep_alive = persistent_keepalive;
	peer.endport_port = port;
    ipaddr_aton(endpoint, &(peer.endpoint_ip));

	// TODO nomi da cambiare
	// TODO continuare da esp_wireguard.c:82 e unrollare qui tutta esp_wireguard_peer_init()
	// TODO Per ora non serve, è solo risoluzione del nome di rete dell'endpoint. Per ora assegnato staticamente (da una stringa)
//    struct addrinfo *endpaddrinfo = NULL;
//	ip_addr_t endpoint_ip;
//	memset(&endpoint_ip, 0, sizeof(endpoint_ip));
//
//	/* XXX lwip_getaddrinfo returns only the first address of a host at the moment */
//	if (getaddrinfo(endpoint, NULL, &hints, &endpaddrinfo) != 0) {
//		err = -14;
//
//		/* XXX gai_strerror() is not implemented */
//		sprintf(errmsg, "getaddrinfo: unable to resolve `%s`", endpoint);
//		console_print(errmsg);
//		goto fail;
//	}
//	if (endpaddrinfo->ai_family == AF_INET) {
//		struct in_addr addr4 = ((struct sockaddr_in *) (endpaddrinfo->ai_addr))->sin_addr;
//		inet_addr_to_ip4addr(ip_2_ip4(&endpoint_ip), &addr4);
//	}


	/* Register the new WireGuard peer with the network interface */
	err = wireguardif_add_peer(wg_netif, &peer, &wireguard_peer_index);
	if (err != ERR_OK || wireguard_peer_index == WIREGUARDIF_INVALID_INDEX) {
		sprintf(errmsg, "[wg] wireguardif_add_peer error: %d\n", err);
		console_print(errmsg);
		goto fail;
	}
//	if (ip_addr_isany(&peer.endpoint_ip)) { // check if ip assigned to some netif
//		err = ESP_FAIL;
//		goto fail;
//	}

	sprintf(errmsg, "[wg] Connecting to peer %s:%d...\n", endpoint, port);
	console_print(errmsg);

    err = wireguardif_connect(wg_netif, wireguard_peer_index);
    if (err != ERR_OK) {
		sprintf(errmsg, "[wg] wireguardif_connect error: %d\n", err);
		console_print(errmsg);
        goto fail;
    }

fail:
	return err;
}

static void prvTCPConnectorTask( void * pvParameters )
{
	int err = 0;

	struct sockaddr_in dest_addr;
	dest_addr.sin_addr.s_addr = inet_addr(host_ip);
	dest_addr.sin_family = AF_INET;
	dest_addr.sin_port = htons(PORT);

	// start the wireguard tunnel
	unix_wireguard_setup();

	// wait wireguard tunnel to open
    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
        //err = wireguardif_peer_is_up(&ctx);

		err = wireguardif_peer_is_up(
				wg_netif,
				wireguard_peer_index,
				&peer.endpoint_ip,
				&peer.endport_port);
        if (err == ERR_OK) {
			console_print("[wg] Peer is up\n");
            break;
        } else {
			console_print("[wg] Peer is down\n");
        }
    }

    // Wireguard tunnel is ready: start to do the rest
    // connect TCP socket
	err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(struct sockaddr_in)); // TODO questo sizeof dà errore
	if (err != 0) {
		sprintf(errmsg, "[demo] Socket unable to connect TCP socket: errno %d\n", errno);
		console_print(errmsg);
		//return;
		exit(-23);
	}
	sprintf(errmsg, "[demo] Successfully connected TCP socket\n");
	console_print(errmsg);

	sock_isready = 1;

	vTaskDelete(NULL);
}

/*** SEE THE COMMENTS AT THE TOP OF THIS FILE ***/
void main_blinky( void )
{
    const TickType_t xTimerPeriod = mainTIMER_SEND_FREQUENCY_MS;

    // Dichiarazioni per connessione di prova
	static struct netif netif_1 = {0}; // memoria per la struct netif, accedere usando \p tap0
	static struct netif *tap0 = NULL; ///< Puntatore da usare in tutto il programma
	//static struct tapif tap0_tapif = {0}; ///< Struttura dati specifica per una netif di tipo posix-tapif
	ip_addr_t ipaddr = IPADDR4_INIT_BYTES(192, 168, 2, 2);
	ip_addr_t netmask = IPADDR4_INIT_BYTES(255, 255, 255, 0);
	ip_addr_t gateway = IPADDR4_INIT_BYTES(192, 168, 2, 1);

	int err = 0;
	int addr_family = AF_INET;
	int ip_protocol = IPPROTO_IP;
	int len;
	// ......................


    /* Create the queue. */
    xQueue = xQueueCreate( mainQUEUE_LENGTH, sizeof( uint32_t ) );

    if( xQueue != NULL )
    {
        /* Start the two tasks as described in the comments at the top of this
         * file. */
        xTaskCreate( prvQueueReceiveTask,             /* The function that implements the task. */
                     "Rx",                            /* The text name assigned to the task - for debug only as it is not used by the kernel. */
                     configMINIMAL_STACK_SIZE,        /* The size of the stack to allocate to the task. */
                     NULL,                            /* The parameter passed to the task - not used in this simple case. */
                     mainQUEUE_RECEIVE_TASK_PRIORITY, /* The priority assigned to the task. */
                     NULL );                          /* The task handle is not required, so NULL is passed. */

        xTaskCreate( prvQueueSendTask, "TX", configMINIMAL_STACK_SIZE, NULL, mainQUEUE_SEND_TASK_PRIORITY, NULL );

        /* Create the software timer, but don't start it yet. */
        xTimer = xTimerCreate( "Timer",                     /* The text name assigned to the software timer - for debug only as it is not used by the kernel. */
                               xTimerPeriod,                /* The period of the software timer in ticks. */
                               pdTRUE,                      /* xAutoReload is set to pdTRUE. */
                               NULL,                        /* The timer's ID is not used. */
                               prvQueueSendTimerCallback ); /* The function executed when the timer expires. */

		// Esempio tapif di lwip: https://github.com/takayuki/lwip-tap

		// doc: https://www.nongnu.org/lwip/2_0_x/group__lwip__os.html
		//   inizializza tutto per #undef NO_SYS (tra cui: crea thread tcp/ip)
		// idea viene da qui (vedi main()): https://github.com/takayuki/lwip-tap/blob/master/lwip-tap.c
		tcpip_init(NULL, NULL);
		// TODO: TCPIP_MBOX_SIZE defaults to 0
		// doc: https://www.nongnu.org/lwip/2_1_x/group__lwip__opts__thread.html
		// usare modalita` OS: https://www.nongnu.org/lwip/2_1_x/group__lwip__os.html
		//   chiede di implementare TUTTE le funzioni in https://www.nongnu.org/lwip/2_1_x/group__sys__layer.html
		//   (direi ok: vedi sys_arch.c)
		//   (alcune come SYS_ARCH_DECL_PROTECT() non sono reimplementate, default e` sys.h:462
		// TODO leggere common pitfalls: https://www.nongnu.org/lwip/2_0_x/pitfalls.html
		//   Di sicuro bisogna capire se in netif_add serve tcpip_input (OS) o ethernet_input (NO_SYS)

		// Crea socket tcp per loggare messaggi ricevuti sulla queue
		tap0 = netif_add(&netif_1, &ipaddr, &netmask, &gateway, /*&tap0_tapif*/ NULL, tapif_init, tcpip_input /*&ethernet_input*/ /*&ip_input*/);
		netif_set_up(tap0);

		//netif_set_addr(tap0, ipaddr, netmask, gateway);

		sock_isready = 0; // set to 1 when the socket is opened
		sock =  socket(addr_family, SOCK_STREAM, ip_protocol);
		if (sock < 0) {
			sprintf(errmsg, "[demo] Unable to create TCP socket: errno %d\n", errno);
			console_print(errmsg);
			return;
		}
		sprintf(errmsg, "[demo] TCP socket created, connecting to %s:%d\n", host_ip, PORT);
		console_print(errmsg);

        xTaskCreate( prvTCPConnectorTask, "TCP connector", configMINIMAL_STACK_SIZE, /*(void*) sock*/ NULL, tskIDLE_PRIORITY+3, NULL );
        /*
		err = connect(sock, (struct sockaddr *)&dest_addr, sizeof(struct sockaddr_in)); // TODO questo sizeof dà errore
		if (err != 0) {
			sprintf(errmsg, "Socket unable to connect TCP socket: errno %d\n", errno);
			console_print(errmsg);
			return;
		}
		sprintf(errmsg, "Successfully connected TCP socket\n");
		console_print(errmsg);
		*/
		// TODO bisognerebbe anche chiudere il socket
		// ......................


        if( xTimer != NULL )
        {
            xTimerStart( xTimer, 0 );
        }

        /* Start the tasks and timer running. */
        vTaskStartScheduler();
    }

    /* If all is well, the scheduler will now be running, and the following
     * line will never be reached.  If the following line does execute, then
     * there was insufficient FreeRTOS heap memory available for the idle and/or
     * timer tasks	to be created.  See the memory management section on the
     * FreeRTOS web site for more details. */
    for( ; ; )
    {
    }
}
/*-----------------------------------------------------------*/

static void prvQueueSendTask( void * pvParameters )
{
    TickType_t xNextWakeTime;
    const TickType_t xBlockTime = mainTASK_SEND_FREQUENCY_MS;
    const uint32_t ulValueToSend = mainVALUE_SENT_FROM_TASK;

    /* Prevent the compiler warning about the unused parameter. */
    ( void ) pvParameters;

    /* Initialise xNextWakeTime - this only needs to be done once. */
    xNextWakeTime = xTaskGetTickCount();

    for( ; ; )
    {
        /* Place this task in the blocked state until it is time to run again.
        *  The block time is specified in ticks, pdMS_TO_TICKS() was used to
        *  convert a time specified in milliseconds into a time specified in ticks.
        *  While in the Blocked state this task will not consume any CPU time. */
        vTaskDelayUntil( &xNextWakeTime, xBlockTime );

        /* Send to the queue - causing the queue receive task to unblock and
         * write to the console.  0 is used as the block time so the send operation
         * will not block - it shouldn't need to block as the queue should always
         * have at least one space at this point in the code. */
        xQueueSend( xQueue, &ulValueToSend, 0U );
    }
}
/*-----------------------------------------------------------*/

static void prvQueueSendTimerCallback( TimerHandle_t xTimerHandle )
{
    const uint32_t ulValueToSend = mainVALUE_SENT_FROM_TIMER;

    /* This is the software timer callback function.  The software timer has a
     * period of two seconds and is reset each time a key is pressed.  This
     * callback function will execute if the timer expires, which will only happen
     * if a key is not pressed for two seconds. */

    /* Avoid compiler warnings resulting from the unused parameter. */
    ( void ) xTimerHandle;

    /* Send to the queue - causing the queue receive task to unblock and
     * write out a message.  This function is called from the timer/daemon task, so
     * must not block.  Hence the block time is set to 0. */
    xQueueSend( xQueue, &ulValueToSend, 0U );
}
/*-----------------------------------------------------------*/

static void prvQueueReceiveTask( void * pvParameters )
{
    uint32_t ulReceivedValue;
	int err = 0;

    /* Prevent the compiler warning about the unused parameter. */
    ( void ) pvParameters;

    for( ; ; )
    {
        /* Wait until something arrives in the queue - this task will block
         * indefinitely provided INCLUDE_vTaskSuspend is set to 1 in
         * FreeRTOSConfig.h.  It will not use any CPU time while it is in the
         * Blocked state. */
        xQueueReceive( xQueue, &ulReceivedValue, portMAX_DELAY );

        /* To get here something must have been received from the queue, but
         * is it an expected value?  Normally calling printf() from a task is not
         * a good idea.  Here there is lots of stack space and only one task is
         * using console IO so it is ok.  However, note the comments at the top of
         * this file about the risks of making Linux system calls (such as
         * console output) from a FreeRTOS task. */
        if( ulReceivedValue == mainVALUE_SENT_FROM_TASK )
        {
            console_print( "Message received from task\n" );
			strcpy(payload, "Message received from task\n");
        }
        else if( ulReceivedValue == mainVALUE_SENT_FROM_TIMER )
        {
            console_print( "Message received from software timer\n" );
			strcpy(payload, "Message received from software timer\n");
        }
        else
        {
            console_print( "Unexpected message\n" );
			strcpy(payload, "Unexpected message\n");
        }

		// Manda lo stesso messaggio che stampi anche sul socket TCP
		if(sock >= 0 && sock_isready)
		{
			// TODO errore tapif: Interrupted system call
			// https://unix.stackexchange.com/questions/509375/what-is-interrupted-system-call

			err = send(sock, payload, strlen(payload), 0);
			if (err < 0) {
				sprintf(errmsg, "[demo] Error occurred during TCP send: errno %d", errno);
				console_print(errmsg);
				return;
			}
		}
    }
}
/*-----------------------------------------------------------*/
