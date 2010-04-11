/*
 * This is a textual header that is prepended to images where appropriate.
 *
 * It is meant to ease identification of our firmwares in dumps as well
 * as filling some space that is used for the same purpose by the vendor.
 *
 */
.section .compal.header
.ascii "OSMOCOM"
. = 0x20
.ascii GIT_REVISION
