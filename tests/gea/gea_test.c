#include <osmocom/core/bits.h>
#include <osmocom/core/utils.h>
#include <osmocom/crypt/gprs_cipher.h>

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <stdbool.h>

static inline void print_check(char *res, uint8_t *out, uint16_t len)
{
    uint8_t buf[len];
    osmo_hexparse(res, buf, len);
    if (0 != memcmp(buf, out, len)) {
	printf("FAIL:\n");
	printf("OUT: %s\n", osmo_hexdump_nospc(out, len));
	printf("EXP: %s\n", osmo_hexdump_nospc(buf, len));
    } else
	    printf("\n");
}

static inline void test_gea(bool v4, char *kc, uint32_t iv, int dir,
			    uint16_t len, char *res)
{
    uint8_t out[len], ck[256];
    printf("len %d, dir %d, INPUT 0x%X -> ", len, dir, iv);
    osmo_hexparse(kc, ck, len);
    int t = gprs_cipher_run(out, len, v4 ? GPRS_ALGO_GEA4 : GPRS_ALGO_GEA3, ck,
			    iv, dir);
    printf("%s ", t < 0 ? strerror(-t) : "OK");
    print_check(res, out, len);
}

static inline void real_gea(uint32_t iov_ui, uint8_t sapi, uint32_t lfn,
			    uint32_t oc, enum gprs_cipher_direction d, char *kc,
			    uint16_t len, char *res)
{
	test_gea(false, kc, gprs_cipher_gen_input_ui(iov_ui, sapi, lfn, oc), d,
		 len, res);
}

int main(int argc, char **argv)
{
    printf("GEA3 support: %d\n", gprs_cipher_supported(GPRS_ALGO_GEA3));
    printf("GEA4 support: %d\n", gprs_cipher_supported(GPRS_ALGO_GEA4));

    /* GEA3 test vectors according to 3GPP TS 55.217 V6.1.0 and 55.218 V6.1.0 */
    test_gea(false, "2BD6459F82C5BC00", 0x8E9421A3, 0, 59, "5F359709DE950D0105B17B6C90194280F880B48DCCDC2AFEED415DBEF4354EEBB21D073CCBBFB2D706BD7AFFD371FC96E3970D143DCB2624054826");
    test_gea(false, "952C49104881FF48", 0x5064DB71, 0, 59, "FDC03D738C8E14FF0320E59AAF75760799E9DA78DD8F888471C4AEAAC1849633A26CD84F459D265B83D7D9B9A0B1E54F4D75E331640DF19E0DB0E0");
    test_gea(false, "EFA8B2229E720C2A", 0x4BDBD5E5, 1, 59, "4718A2ADFC90590949DDADAB406EC3B925F1AF1214673909DAAB96BB4C18B1374BB1E99445A81CC856E47C6E49E9DBB9873D0831B2175CA1E109BA");
    test_gea(false, "3451F23A43BD2C87", 0x893FE14F, 0, 59, "B46B1E284E3F8B63B86D9DF0915CFCEDDF2F061895BF9F82BF2593AE4847E94A4626C393CF8941CE15EA7812690D8415B88C5730FE1F5D410E16A2");
    test_gea(false, "CAA2639BE82435CF", 0x8FE17885, 1, 59, "9FEFAF155A26CF35603E727CDAA87BA067FD84FF98A50B7FF0EC8E95A0FB70E79CB93DEE2B7E9AB59D050E1262401571F349C68229DDF0DECC4E85");
    test_gea(false, "1ACA8B448B767B39", 0x4F7BC3B5, 0, 59, "514F6C3A3B5A55CA190092F7BB6E80EF3EDB738FCDCE2FF90BB387DDE75BBC32A04A67B898A3DFB8198FFFC37D437CF69E7F9C13B51A868720E750");

    /* GEA4 test vectors according to 3GPP TS 55.226 V9.0.0 */
    test_gea(true, "D3C5D592327FB11C4035C6680AF8C6D1", 0x0A3A59B4, 0, 51, "6E217CE41EBEFB5EC8094C15974290065E42BABC9AE35654A53085CE68DFA4426A2FF0AD4AF3341006A3F84B7613ACB4FBDC34");
    test_gea(true, "3D43C388C9581E337FF1F97EB5C1F85E", 0x48571AB9, 0, 59, "FC7314EF00A63ED0116F236C5D25C54EEC56A5B71F9F18B4D7941F84E422ACBDE5EEA9A204679002D14F312F3DEE2A1AC917C3FBDC3696143C0F5D");
    test_gea(true, "A4496A64DF4F399F3B4506814A3E07A1", 0xEB04ADE2, 1, 59, "2AEB5970FB06B718027D048488AAF24FB3B74EA4A6B1242FF85B108FF816A303C72757D9AAD862B835D1D287DBC141D0A28D79D87BB137CD1198CD");

    /* GEA3 test with real data */
    real_gea(0, 3, 26, 0, GPRS_CIPH_SGSN2MS, "0c09c6ed723a8400", 199, "b08edbbb7f0532ccbd9fef6e1917fa6815e6d7fa4c9ee629f89299ef9a5541bb23f05875487113c5d5166b7fd0bd04215c60cf8dc404f6cbbfb137191e87f5250a311e34c583f7d8a35346e35f287722b6f234b95b67037ee0711b25fbf8ee3fcb354ee2ba1981e57ce3eeaa4f7401a6a8328994c25b359821e4e7ee242f7daf4e597e006b9cce0b9e86d97cd1ff83a021203b93e83457f64f9794d4388e9b7c509ca5ad278b10082fa20d7ac48aea4cb787c19fcbcea95d63bc17e840380adc86688bcf293f2e");
    real_gea(0, 3, 25, 0, GPRS_CIPH_SGSN2MS, "0c09c6ed723a8400", 77, "91d2a1a85f6bb00429d76dcd1ca27f50c52e079ee9e990afc8635e25fa8c1007fac52e77035fd09821db893843c7a0666c4e69b5ecd3c9e4e7dc3405e4535115a650eebf698674e248ef13575c");
    real_gea(0, 3, 20, 0, GPRS_CIPH_MS2SGSN, "bf4575e165fec400", 134, "c43845418e7fc4b3651bc9c3cc9af0163373126c0b31f85d192280e20c981f426dc4a0514a377f76da3d1672c6a0f463513608b3291bacd5d17bb44c8cc5383c3cc85de94e9c594e0fd61d4f2b74b452c1edf07eb04e0e67f352337cc0fd932936841fa41ee5ff0d8f3fad9625a9dec1f12726b74595a1c40d429926ba7e8461f3fa2ae2c0d3");
    real_gea(0, 3, 21, 0, GPRS_CIPH_MS2SGSN, "bf4575e165fec400", 65, "7b4fc1922c183e6f61e8d2317216ed1d2497477d6f84947f8318df42621ad9affc0c42ba2fd63e06bce4720598d5ae919ca2996f2f1feaea2aa79827692471fd0a");

    return 0;
}
