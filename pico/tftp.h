#ifndef _TFTP_H
#define _TFTP_H

#define PRODOS_BLOCKSIZE 512

//
//TFTP Parameters
//

//TFTP Timeout in ms
#define TFTP_TIMEOUT_DEFAULT 2000
#define TFTP_TIMEOUT_MIN 500
#define TFTP_TIMEOUT_MAX 5000

//TFTP Max Retries
#define TFTP_MAXATTEMPT_DEFAULT 6
#define TFTP_MAXATTEMPT_MIN     2
#define TFTP_MAXATTEMPT_MAX    10

#define TFTP_SERVERPORT_DEFAULT    69
#define TFTP_ENABLE1KBLOCK_DEFAULT true

/* The default timeout of most TFTP server is 3s. For the last Ack, the server
does not response if the Ack message is delivered succesfully. So, we need to
wait slightly longer than 3s before we terminate the process. This is the minimum
timeout value of last Ack message. Current implementation uses TFTP Timeout or 
TFTP_TIMEOUT_LASTACK_MIN, whichever is greater */
#define TFTP_TIMEOUT_LASTACK_MIN 3100 


#endif
