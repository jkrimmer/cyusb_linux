/*
 * Filename             : download_fx2.cpp
 * Description          : Downloads FX2LP firmware to RAM, Small EEPROM or Large EEPROM.
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <libusb-1.0/libusb.h>
#include "../include/cyusb.h"

#define FX2_CPUCS_ADDR		(0xE600)
#define FX2_INT_RAMSIZE		(0x4000)

#define VENDORCMD_TIMEOUT	(5000)
#define MAX_LINE_LENGTH		(512)
#define MAX_BYTES_PER_LINE	(256)
#define EEPROM_WRITE_SIZE	(1024)

#define ROUND_UP(n,v)		((((n) + ((v) - 1)) / (v)) * (v))
#define CHAR_TO_HEXVAL(c)	((((c) >= '0') && ((c) <= '9')) ? ((c) - '0') : ((((c) - 'A') & 0x0F) + 10))
#define GET_HEX_BYTE(char_p)	(((CHAR_TO_HEXVAL((char_p)[0])) << 4) | (CHAR_TO_HEXVAL((char_p)[1])))
#define GET_HEX_WORD(char_p)	(((CHAR_TO_HEXVAL((char_p)[0])) << 12) | ((CHAR_TO_HEXVAL((char_p)[1])) << 8) | \
		((CHAR_TO_HEXVAL((char_p)[2])) << 4) | (CHAR_TO_HEXVAL((char_p)[3])))

#define FX2_MAX_FW_SIZE		(0x10000)

/* List of supported programming targets */
typedef enum {
	FW_TARGET_NONE = 0,	// Invalid target
	FW_TARGET_RAM,		// Program firmware (hex) to RAM
	FW_TARGET_SM_I2C,	// Program VID and PID to Small I2C EEPROM
	FW_TARGET_LR_I2C	// Program VID and PID to Large I2C EEPROM
} fx2_fw_tgt_p;

/* List of FX2LP firmware formats. */
typedef enum {
	FW_FORMAT_INVALID = 0,  // Invalid type, typically due to file access error.
	FW_FORMAT_HEX = 0,      // Intel Hex Format, download to RAM only.
	FW_FORMAT_BIN,          // Binary format, download to RAM only.
	FW_FORMAT_C2LOAD,       // Firmware in C2 load format.
	FW_FORMAT_C0LOAD	// USB IDs in C0 load format.
} fx2_fw_fmt;

static char fx2_vendax[][256] = {
	":0a0d3e0000010202030304040505",
	":10064d00E4F52CF52BF52AF529C203C200C202C2",
	":10065d0001120C6C7E0A7F008E0A8F0B75120A75",
	":10066d00131275080A75091C75100A75114A7514",
	":10067d000A751578EE54C07003020752752D0075",
	":10068d002E808E2F8F30C3749A9FFF740A9ECF24",
	":10069d0002CF3400FEE48F288E27F526F525F524",
	":1006ad00F523F522F521AF28AE27AD26AC25AB24",
	":1006bd00AA23A922A821C3120D035037E5302524",
	":1006cd00F582E52F3523F583E0FFE52E2524F582",
	":1006dd00E52D3523F583EFF0E4FAF9F8E5242401",
	":1006ed00F524EA3523F523E93522F522E83521F5",
	":1006fd002180B3852D0A852E0B74002480FF740A",
	":10070d0034FFFEC3E5139FF513E5129EF512C3E5",
	":10071d000D9FF50DE50C9EF50CC3E50F9FF50FE5",
	":10072d000E9EF50EC3E5099FF509E5089EF508C3",
	":10073d00E5119FF511E5109EF510C3E5159FF515",
	":10074d00E5149EF514D2E843D82090E668E04409",
	":10075d00F090E65CE0443DF0D2AF90E680E054F7",
	":10076d00F0538EF8C2031207FF30010512039AC2",
	":10077d00013003F2120D6450EDC203120C0D2000",
	":10078d001690E682E030E704E020E1EF90E682E0",
	":10079d0030E604E020E0E4120BB6120D6680C722",
	":0b0d330090E50DE030E402C322D322",
	":10039a0090E6B9E0700302048514700302052E24",
	":1003aa00FE70030205C424FB700302047F147003",
	":1003ba0002047914700302046D14700302047324",
	":1003ca00056003020639120D68400302064590E6",
	":1003da00BBE024FE603B14605624FD6016146040",
	":1003ea0024067075E50A90E6B3F0E50B90E6B4F0",
	":1003fa00020645120D33500FE51290E6B3F0E513",
	":10040a0090E6B4F002064590E6A0E04401F00206",
	":10041a0045E50C90E6B3F0E50D90E6B4F0020645",
	":10042a00E50E90E6B3F0E50F90E6B4F002064590",
	":10043a00E6BAE0FF120BE2AA06A9077B01EA494B",
	":10044a00600DEE90E6B3F0EF90E6B4F002064590",
	":10045a00E6A0E04401F002064590E6A0E04401F0",
	":10046a00020645120CF1020645120D5002064512",
	":10047a000D48020645120CDF020645120D6A4003",
	":10048a0002064590E6B8E0247F602B14603C2402",
	":10049a006003020524A200E433FF25E0FFA202E4",
	":1004aa00334F90E740F0E4A3F090E68AF090E68B",
	":1004ba007402F0020645E490E740F0A3F090E68A",
	":1004ca00F090E68B7402F002064590E6BCE0547E",
	":1004da00FF7E00E0D3948040067C007D0180047C",
	":1004ea00007D00EC4EFEED4F243EF582740D3EF5",
	":1004fa0083E493FF3395E0FEEF24A1FFEE34E68F",
	":10050a0082F583E0540190E740F0E4A3F090E68A",
	":10051a00F090E68B7402F002064590E6A0E04401",
	":10052a00F0020645120D6C400302064590E6B8E0",
	":10053a0024FE601D2402600302064590E6BAE0B4",
	":10054a000105C20002064590E6A0E04401F00206",
	":10055a004590E6BAE0705990E6BCE0547EFF7E00",
	":10056a00E0D3948040067C007D0180047C007D00",
	":10057a00EC4EFEED4F243EF582740D3EF583E493",
	":10058a00FF3395E0FEEF24A1FFEE34E68F82F583",
	":10059a00E054FEF090E6BCE05480FF131313541F",
	":1005aa00FFE0540F2F90E683F0E04420F0020645",
	":1005ba0090E6A0E04401F0020645120D6E507C90",
	":1005ca00E6B8E024FE60202402705B90E6BAE0B4",
	":1005da000104D200806590E6BAE06402605D90E6",
	":1005ea00A0E04401F0805490E6BCE0547EFF7E00",
	":1005fa00E0D3948040067C007D0180047C007D00",
	":10060a00EC4EFEED4F243EF582740D3EF583E493",
	":10061a00FF3395E0FEEF24A1FFEE34E68F82F583",
	":10062a00E04401F0801590E6A0E04401F0800C12",
	":10063a000080500790E6A0E04401F090E6A0E044",
	":03064a0080F022",
	":03003300020D60",
	":040d600053D8EF32",
	":100c6c00D200E4F51A90E678E05410FFC4540F44",
	":090c7c0050F51713E433F51922",
	":0107ff0022",
	":060d6400D322D322D322",
	":080d480090E6BAE0F518D322",
	":100cdf0090E740E518F0E490E68AF090E68B04F0",
	":020cef00D322",
	":080d500090E6BAE0F516D322",
	":100cf10090E740E516F0E490E68AF090E68B04F0",
	":020d0100D322",
	":060d6a00D322D322D322",
	":1000800090E6B9E0245EB40B0040030203989000",
	":100090009C75F003A4C58325F0C5837302019202",
	":1000a000019202010D0200BD0200D70200F30201",
	":1000b0003C02018C02011602012902016290E740",
	":1000c000E519F0E490E68AF090E68B04F090E6A0",
	":1000d000E04480F002039890E60AE090E740F0E4",
	":1000e00090E68AF090E68B04F090E6A0E04480F0",
	":1000f00002039890E740740FF0E490E68AF090E6",
	":100100008B04F090E6A0E04480F002039890E6BA",
	":10011000E0F51702039890E67AE054FEF0E490E6",
	":100120008AF090E68BF002039890E67AE04401F0",
	":10013000E490E68AF090E68BF002039890E74074",
	":1001400007F0E490E68AF090E68B04F090E6A0E0",
	":100150004480F07FE87E031207ADD204120B8702",
	":10016000039890E6B5E054FEF090E6BFE090E68A",
	":10017000F090E6BEE090E68BF090E6BBE090E6B3",
	":10018000F090E6BAE090E6B4F002039875190143",
	":10019000170190E6BAE0753100F532A3E0FEE4EE",
	":1001a000423190E6BEE0753300F534A3E0FEE4EE",
	":1001b000423390E6B8E064C06003020282E53445",
	":1001c00033700302039890E6A0E020E1F9C3E534",
	":1001d0009440E533940050088533358534368006",
	":1001e00075350075364090E6B9E0B4A335E4F537",
	":1001f000F538C3E5389536E53795355060E53225",
	":1002000038F582E5313537F583E0FF74402538F5",
	":1002100082E434E7F583EFF00538E53870020537",
	":1002200080D0E4F537F538C3E5389536E5379535",
	":10023000501874402538F582E434E7F58374CDF0",
	":100240000538E5387002053780DDAD367AE77940",
	":100250007EE77F40AB07AF32AE311208B8E490E6",
	":100260008AF090E68BE536F02532F532E5353531",
	":10027000F531C3E5349536F534E5339535F53302",
	":1002800001BD90E6B8E064406003020398E51A70",
	":10029000051209678F1AE53445337003020398E4",
	":1002a00090E68AF090E68BF090E6A0E020E1F990",
	":1002b000E68BE0753500F53690E6B9E0B4A338E4",
	":1002c000F537F538C3E5389536E5379535400302",
	":1002d000037C74402538F582E434E7F583E0FFE5",
	":1002e000322538F582E5313537F583EFF00538E5",
	":1002f000387002053780CDE4F537F538C3E53895",
	":1003000036E53795355075851A39E51A64016044",
	":10031000E5322538FFE5313537FEE51A24FFFDE4",
	":1003200034FF5EFEEF5D4E6010E5322538FFE51A",
	":10033000145FFFC3E51A9FF539C3E5369538FFE5",
	":10034000359537FEC3EF9539EE94005007C3E536",
	":100350009538F539E5322538FFE5313537FE7440",
	":100360002538F582E434E7AD82FCAB39120A9CE5",
	":10037000392538F538E43537F5378080E5362532",
	":10038000F532E5353531F531C3E5349536F534E5",
	":0a039000339535F533020296C322",
	":100c3200C0E0C083C08290E6B5E04401F0D20153",
	":0f0c420091EF90E65D7401F0D082D083D0E032",
	":100c9d00C0E0C083C0825391EF90E65D7404F0D0",
	":100cad0082D083D0E032C0E0C083C0825391EF90",
	":0c0cbd00E65D7402F0D082D083D0E032",
	":100b1900C0E0C083C08290E680E030E70E85080C",
	":100b290085090D85100E85110F800C85100C8511",
	":100b39000D85080E85090F5391EF90E65D7410F0",
	":070b4900D082D083D0E032",
	":100c8500C0E0C083C082D2035391EF90E65D7408",
	":080c9500F0D082D083D0E032",
	":100b5000C0E0C083C08290E680E030E70E85080C",
	":100b600085090D85100E85110F800C85100C8511",
	":100b70000D85080E85090F5391EF90E65D7420F0",
	":070b8000D082D083D0E032",
	":0109ff0032",
	":100d700032323232323232323232323232323232",
	":100d800032323232323232323232323232323232",
	":030d9000323232",
	":100a00001201000200000040B404041000000102",
	":100a100000010A06000200000040010009022E00",
	":100a200001010080320904000004FF0000000705",
	":100a300002020002000705040200020007058602",
	":100a40000002000705880200020009022E000101",
	":100a50000080320904000004FF00000007050202",
	":100a600040000007050402400000070586024000",
	":100a700000070588024000000403090410034300",
	":100a80007900700072006500730073000E034500",
	":0c0a90005A002D005500530042000000",
	":100bb60090E682E030E004E020E60B90E682E030",
	":100bc600E119E030E71590E680E04401F07F147E",
	":0c0bd600001207AD90E680E054FEF022",
	":100b870030040990E680E0440AF0800790E680E0",
	":100b97004408F07FDC7E051207AD90E65D74FFF0",
	":0f0ba70090E65FF05391EF90E680E054F7F022",
	":1007ad008E3A8F3B90E600E054187012E53B2401",
	":1007bd00FFE4353AC313F53AEF13F53B801590E6",
	":1007cd0000E05418FFBF100BE53B25E0F53BE53A",
	":1007dd0033F53AE53B153BAE3A7002153A4E6005",
	":0607ed00120C2180EE22",
	":100be200A907AE14AF158F828E83A3E064037017",
	":100bf200AD0119ED7001228F828E83E07C002FFD",
	":100c0200EC3EFEAF0580DFE4FEFF2290E682E044",
	":100c1200C0F090E681F043870100000000002274",
	":100c220000F58690FDA57C05A3E582458370F922",
	":03004300020800",
	":03005300020800",
	":10080000020C3200020CB300020C9D00020C8500",
	":10081000020B1900020B50000209FF00020D7000",
	":10082000020D7100020D7200020D7300020D7400",
	":10083000020D7500020D7600020D7700020D7800",
	":10084000020D7900020D7000020D7A00020D7B00",
	":10085000020D7C00020D7D00020D7E00020D7F00",
	":10086000020D8000020D7000020D7000020D7000",
	":10087000020D8100020D8200020D8300020D8400",
	":10088000020D8500020D8600020D8700020D8800",
	":10089000020D8900020D8A00020D8B00020D8C00",
	":1008a000020D8D00020D8E00020D8F00020D9000",
	":0808b000020D9100020D9200",
	":100a9c008E3C8F3D8C3E8D3F8B40C28743B28012",
	":100aac000D58120D24120CC95004D2048059E519",
	":100abc00600FE53C90E679F0120CC95004D20480",
	":100acc0046E53D90E679F0120CC95004D2048037",
	":100adc00E4F541E541C395405021053FE53FAE3E",
	":100aec007002053E14F5828E83E090E679F0120D",
	":100afc00145004D2048010054180D890E678E044",
	":0d0b0c0040F0120C51C20453B27FA20422",
	":0f0d240090E6787480F0E51725E090E679F022",
	":100c5100120D58120D24120D1490E678E04440F0",
	":0b0c6100120D5890E678E030E1E922",
	":080d580090E678E020E6F922",
	":1008b8008E3C8F3D8D3E8A3F8B40120D58120D24",
	":1008c800120CC9500122E519600CE53C90E679F0",
	":1008d800120CC9500122E53D90E679F0120CC950",
	":1008e800012290E6787480F0E51725E0440190E6",
	":1008f80079F0120D1450012290E679E0F541120D",
	":1009080014500122E4F541E53E14FFE541C39F50",
	":100918001C90E679E0FFE5402541F582E4353FF5",
	":1009280083EFF0120D14500122054180DA90E678",
	":100938007420F0120D1450012290E679E0FFE540",
	":100948002541F582E4353FF583EFF0120D145001",
	":0f0958002290E6787440F090E679E0F541C322",
	":100d140090E678E0FF30E0F8EF30E202D322C322",
	":100cc90090E678E0FF30E0F8EF30E202D322EF20",
	":060cd900E102D322C322",
	":10096700E51970037F01227A107B407D40E4FFFE",
	":100977001208B8E4F53A7400253AF582E43410F5",
	":1009870083E53AF0053AE53AB440EB7C107D007B",
	":1009970040E4FFFE120A9CE4F53AE53AF4FF7400",
	":1009a700253AF582E43410F583EFF0053AE53AB4",
	":1009b70040E87A107B007D40E4FFFE1208B89010",
	":1009c70000E0F53AE53A30E005753B018008633A",
	":1009d7003F053A853A3BE4F53AE53AC394405015",
	":1009e700AF3A7E007C107D40AB3B120A9CE53B25",
	":0809f7003AF53A80E4AF3B22",
	":030000000207F3",
	":0c07f300787FE4F6D8FD75814102064D",
	":100d0300EB9FF5F0EA9E42F0E99D42F0E89C45F0",
	":010d130022",
	":00000001FF"
};
#define FX2_VENDAX_SIZE		(sizeof (fx2_vendax) / (256 * sizeof(char)))

static void
fx2_dnld_print_usage (
		const char *arg0)
{
	printf ("%s: FX2LP firmware programmer\n", arg0);
	printf ("Usage:\n");
	printf ("\t%s -h: Print usage information\n\n", arg0);
	printf ("\t%s -i <filename> -t <target> [-v <VID> -p <PID>]:\n", arg0);
	printf ("\tProgram firmware from <filename> to <target> of device with <VID> <PID>\n");
	printf ("\t\twhere <target> is one of:\n");
	printf ("\t\t\t\"RAM \": Program to internal or external RAM\n");
	printf ("\t\t\t\"SI2C\": Program to small I2C EEPROM, IIC file to be provided\n");
	printf ("\t\t\t\"LI2C\": Program to large I2C EEPROM, IIC file to be provided\n");
	printf ("\t\tVID and PID are optional, pick devices from '/etc/cyusb.conf' as default\n");
	printf ("\n");
}

static fx2_fw_fmt
get_fw_format (
		FILE *fw_fil_p)
{
	fx2_fw_fmt fmt = FW_FORMAT_HEX;
	unsigned char hexdata[MAX_LINE_LENGTH], *fw_p;
	unsigned char num_bytes;
	size_t len;

	if (fw_fil_p != 0) {
		fseek (fw_fil_p, 0, SEEK_SET);

		/* Check for valid HEX file first. Each line should begin with ':' and
		 * have enough data to cover the byte count per line. */
		while (fgets ((char *)hexdata, MAX_LINE_LENGTH, fw_fil_p) != NULL) {

			if (hexdata[0] != ':')
				fmt = FW_FORMAT_BIN;

			fw_p      = &hexdata[1];
			len       = strlen ((const char *)hexdata);
			num_bytes = GET_HEX_BYTE(fw_p);

			if (len < (num_bytes * 2 + 9))
				fmt = FW_FORMAT_BIN;
		}

		if (fmt != FW_FORMAT_HEX) {
			fseek (fw_fil_p, 0, SEEK_SET);
			if (fgets ((char *)hexdata, MAX_LINE_LENGTH, fw_fil_p) == NULL) {
				fmt = FW_FORMAT_INVALID;
			} else {
				switch (hexdata[0]) {
					case 0xC0:
						fmt = FW_FORMAT_C0LOAD;
						printf ("Found C0 Load IIC format file\n");
						break;
					case 0xC2:
						printf ("Found C2 Load IIC format file\n");
						fmt = FW_FORMAT_C2LOAD;
						break;
					default:
						printf ("Found binary format file\n");
						fmt = FW_FORMAT_BIN;
						break;
				}
			}
		} else
			printf ("Found Hex format file\n");
	} else
		fmt = FW_FORMAT_INVALID;

	return fmt;
}

/* Function to read the FX2LP firmware from in_file into the fw_buf buffer.
   max_addr is used to return the maximum valid address found in the input.
 */
static fx2_fw_fmt
read_fx2_firmware (
		const char    *filename,
		unsigned char *fw_buf,
		unsigned int  *max_addr)
{
	FILE         *in_file = NULL;
	fx2_fw_fmt    fmt;
	int           ret, i;
	struct stat   filbuf;
	unsigned char scratch[MAX_LINE_LENGTH], *tmp;
	unsigned int  address, length;


	if ((filename == NULL) || (fw_buf == NULL) || (max_addr == NULL)) {
		fprintf (stderr, "Invalid parameter\n");
		return FW_FORMAT_INVALID;
	}

	ret = stat (filename, &filbuf);
	if (ret != 0) {
		fprintf (stderr, "Failed to stat file %s\n", filename);
		return FW_FORMAT_INVALID;
	}

	in_file = fopen (filename, "rb");
	if (in_file == NULL) {
		fprintf (stderr, "Failed to open file %s\n", filename);
		return FW_FORMAT_INVALID;
	}

	*max_addr = 0;
	fmt = get_fw_format (in_file);
	fseek (in_file, 0, SEEK_SET);
	memset (fw_buf, 0, FX2_MAX_FW_SIZE);

	switch (fmt)
	{
		case FW_FORMAT_HEX:
			{
				fmt    = FW_FORMAT_INVALID;
				length = 0;

				while (fgets ((char *)scratch, MAX_LINE_LENGTH, in_file) != NULL) {
					/* If this is the end of file record, stop reading. */
					if ((scratch[7] == '0') && (scratch[8] == '1')) {
						fmt = FW_FORMAT_BIN;
						break;
					}

					/* Ignore all except data records. */
					if ((scratch[7] != '0') || (scratch[8] != '0'))
						continue;

					tmp = &scratch[1];
					length  = GET_HEX_BYTE (tmp);
					tmp += 2;
					address = GET_HEX_WORD (tmp);

					if ((address + length) > FX2_MAX_FW_SIZE) {
						fprintf (stderr, "Firmware address out of range\n");
						break;
					}

					if ((address + length) > *max_addr)
						*max_addr = address + length;

					tmp += 6;
					for (i = 0; i < length; i++) {
						fw_buf[address + i] = GET_HEX_BYTE (tmp);
						tmp += 2;
					}
				}
			}
			break;

		case FW_FORMAT_C2LOAD:
			{
				fmt = FW_FORMAT_INVALID;

				/* Read and skip the 8 byte header. */
				ret = fread (scratch, 1, 8, in_file);
				if (ret == 8) {
					ret = fread (scratch, 1, 4, in_file);
					while (ret == 4) {
						length  = (scratch[0] << 8) | scratch[1];
						address = (scratch[2] << 8) | scratch[3];

						/* We have received the footer. Stop reading file. */
						if ((address == FX2_CPUCS_ADDR) && (length == 0x8001)) {
							fmt = FW_FORMAT_BIN;
							break;
						}

						if ((address + length) > FX2_MAX_FW_SIZE) {
							fprintf (stderr, "Firmware address out of range\n");
							break;
						}

						if ((address + length) > *max_addr)
							*max_addr = address + length;
						ret = fread (fw_buf + address, 1, length, in_file);
						if (ret != length) {
							fprintf (stderr, "Failed to read file\n");
							break;
						}

						ret = fread (scratch, 1, 4, in_file);
					}
				}
			}
			break;

		case FW_FORMAT_BIN:
			{
				/* Binary file, just read contents into memory as it is. */
				if (filbuf.st_size <= FX2_MAX_FW_SIZE) {
					ret = fread (fw_buf, 1, filbuf.st_size, in_file);
					if (ret == filbuf.st_size) {
						*max_addr = ret;
						ret = 0;
					} else {
						fprintf (stderr, "File %s read failed\n", filename);
						fmt = FW_FORMAT_INVALID;
					}
				} else {
					fprintf (stderr, "File %s is too large. Not likely to be a FX2 firmware binary\n", filename);
					fmt = FW_FORMAT_INVALID;
				}
			}
			break;

		default:
			fmt = FW_FORMAT_INVALID;
			break;
	}

	fclose (in_file);
	return (fmt);
}

/* Function to force FX2 CPU into (cpu_enable = 0) or out (cpu_enable != 0) of reset. */
static int
fx2_reset (
		libusb_device_handle *h,
		int           cpu_enable)
{
	unsigned char reset = (cpu_enable) ? 0 : 1;
	int r;

	r = libusb_control_transfer(h, 0x40, 0xA0, FX2_CPUCS_ADDR, 0x00, &reset, 0x01, VENDORCMD_TIMEOUT);
	if ( r != 1 ) {
		fprintf (stderr, "ERROR: FX2 reset command failed\n");
		return -1;
	}

	return 0;
}

/* Function to load the Vend_Ax firmware into the FX3 RAM. */
static int
fx2_load_vendax (
		libusb_device_handle *h)
{
	int r, j;
        unsigned int i;
	unsigned char *fw_p;
	unsigned char  databuf[MAX_BYTES_PER_LINE];
	unsigned char  num_bytes = 0;
	unsigned short address = 0;

	printf("Info: Downloading Vend_ax hex into FX2 RAM\n");

	for ( i = 0; ((i < FX2_VENDAX_SIZE) && (fx2_vendax[i][8] == 0x30)); i++ ) {
		fw_p = (unsigned char *)&fx2_vendax[i][1];
		num_bytes = GET_HEX_BYTE(fw_p);
		fw_p += 2;
		address   = GET_HEX_WORD(fw_p);
		fw_p += 6;
		for ( j = 0; j < num_bytes; j++ ) {
			databuf[j] = GET_HEX_BYTE(fw_p);
			fw_p += 2;
		}

		r = libusb_control_transfer(h, 0x40, 0xA0, address, 0x00, databuf, num_bytes, VENDORCMD_TIMEOUT);
		if ( r != num_bytes ) {
			printf("Error in control_transfer\n");
			return -2;
		}
	}

	printf("Info: Releasing FX2 CPU from reset\n");
	r = fx2_reset (h, 1);
	if ( r != 0 ) {
		fprintf (stderr, "Error: Failed to get FX2 out of reset\n");
		return -3;
	}

	return 0;
}

static int
fx2_ram_download (
		libusb_device_handle *h,
		const char   *filename,
		int           extended)
{
	unsigned char fw_buf[FX2_MAX_FW_SIZE];
	fx2_fw_fmt    fmt;
	unsigned int  address = 0, length = 0;
	int           i, r;

	fmt = read_fx2_firmware (filename, fw_buf, &address);
	if (fmt != FW_FORMAT_BIN) {
		fprintf (stderr, "Error: Invalid firmware file format\n");
		return -1;
	}

	if ((address > FX2_INT_RAMSIZE) && (!extended)) {
		fprintf (stderr, "Error: Firmware too big to fit in internal RAM\n");
		return -2;
	}

	r = fx2_reset (h, 0);
	if (r != 0) {
		fprintf (stderr, "Error: Failed to force FX2 into reset\n");
		return -3;
	}

	/* Wait for some time. */
	sleep(1);

	if ((extended) && (address > FX2_INT_RAMSIZE)) {
		printf ("Loading VEND-AX firmware\n");
		r = fx2_load_vendax(h);
		if ( r != 0 ) {
			fprintf (stderr, "Failed to download Vend_Ax firmware to aid programming\n");
			return -4;
		}

		/* Load the external RAM part first. */
		for (i = FX2_INT_RAMSIZE; i < address; i += EEPROM_WRITE_SIZE) {
			length = ((address - i) > EEPROM_WRITE_SIZE) ? EEPROM_WRITE_SIZE : (address - i);
			r = libusb_control_transfer (h, 0x40, 0xA3, i, 0x00, &fw_buf[i], length, VENDORCMD_TIMEOUT);
			if (r != length) {
				fprintf (stderr, "Vendor write to RAM failed\n");
				return -5;
			}
		}

		/* All data has been loaded on external RAM. Now halt the CPU and load the internal RAM. */
		printf ("Info: Forcing FX2 CPU into reset\n");
		r = fx2_reset(h, 0);
		if ( r != 0 ) {
			fprintf (stderr, "Error: Failed to halt FX2 CPU\n");
			return -6;
		}

		/* We can reduce the size to the internal RAM size now. */
		address = FX2_INT_RAMSIZE;
	}

	/* Load the internal RAM part now. */
	for (i = 0; i < address; i += EEPROM_WRITE_SIZE) {
		length = ((address - i) > EEPROM_WRITE_SIZE) ? EEPROM_WRITE_SIZE : (address - i);
		r = libusb_control_transfer (h, 0x40, 0xA0, i, 0x00, &fw_buf[i], length, VENDORCMD_TIMEOUT);
		if (r != length) {
			fprintf (stderr, "Vendor write to RAM failed\n");
			return -7;
		}
	}

	/* Now release CPU from reset. */
	printf ("Info: Releasing FX2 CPU from reset\n");
	r = fx2_reset(h, 1);
	if ( r != 0 ) {
		fprintf (stderr, "Error: Failed to release FX2 from reset\n");
		return -8;
	}

	return 0;
}

/* Function to download IIC file into an I2C EEPROM. */
static int
fx2_eeprom_download (
		libusb_device_handle *h,
		const char   *filename,
		int           large)
{
	int fd;
	unsigned char buf[EEPROM_WRITE_SIZE];
	int r;
	unsigned short address = 0;
	int nbr;
	int filsz;
	struct stat filbuf;

	stat(filename, &filbuf);
	filsz = filbuf.st_size;

	if ((!large) && (filsz > 256)) {
		fprintf(stderr, "Error: File size greater than small EEPROM size\n");
		return -1;
	}

	fd = open(filename, O_RDONLY);
	if ( fd < 0 ) {
		fprintf(stderr, "Error: Failed to open file %s\n", filename);
		return -2;
	}

	printf("Info: Forcing FX2 CPU into reset\n");
	r = fx2_reset(h, 0);
	if ( r != 0 ) {
		fprintf (stderr, "Error: Failed to force FX2 CPU into reset\n");
		close (fd);
		return -3;
	}

	/* Load the Vend_ax firmware to support the EEPROM write commands. */
	r = fx2_load_vendax(h);
	if ( r != 0 ) {
		fprintf(stderr, "Error: Failed to load Vend_Ax firmware\n");
		close(fd);
		return -4;
	}

	while ( (nbr = read(fd, buf, EEPROM_WRITE_SIZE)) ) {
		if (large)
			nbr = ROUND_UP(nbr, 64);
		else
			nbr = ROUND_UP(nbr, 8);
		r = libusb_control_transfer(h, 0x40, ((large) ? 0xA9 : 0xA2), address, 0x00, buf, nbr, VENDORCMD_TIMEOUT);
		if ( r != nbr ) {
			fprintf(stderr, "Error: Control transfer to write EEPROM failed\n");
			close(fd);
			return -5;
		}

		address += nbr;
	}

	close(fd);
	return 0;
}

int main (
		int    argc,
		char **argv)
{
	libusb_device_handle *h;
	const char *filename = NULL;
	const char *tgt_str  = NULL;
	fx2_fw_tgt_p tgt = FW_TARGET_NONE;

	FILE *fp1 = NULL;
	FILE *fp2 = NULL;

	char buf[256];
	char tbuf1[3];
	char tbuf2[5];
	char tbuf3[3];
	unsigned char reset = 0;
	int r;
	unsigned char num_bytes = 0;
	unsigned short address = 0;
	unsigned char *dbuf = NULL;
	int i;
        unsigned short vid = 0;
        unsigned short pid = 0;

	/* Parse command line arguments. */
	for (i = 1; i < argc; i++) {
		if ((strcasecmp (argv[i], "-h") == 0) || (strcasecmp (argv[i], "--help") == 0)) {
			fx2_dnld_print_usage (argv[0]);
			return 0;
		} else if ((strcasecmp (argv[i], "-t") == 0) || (strcasecmp (argv[i], "--target") == 0)) {
                        if (argc > (i + 1)) {
                                tgt_str = argv[i + 1];
                                if (strcasecmp (argv[i + 1], "ram") == 0)
                                        tgt = FW_TARGET_RAM;
                                if (strcasecmp (argv[i + 1], "si2c") == 0)
                                        tgt = FW_TARGET_SM_I2C;
                                if (strcasecmp (argv[i + 1], "li2c") == 0)
                                        tgt = FW_TARGET_LR_I2C;
                                if (tgt == FW_TARGET_NONE) {
                                        fprintf (stderr, "Error: Unknown target %s\n", argv[i + 1]);
                                        fx2_dnld_print_usage (argv[0]);
                                        return -EINVAL;
                                }
                        }
                        i++;
                } else if ((strcmp (argv[i], "-i") == 0) || (strcmp (argv[i], "--image") == 0)) {
                        if (argc > (i + 1))
                                filename = argv[i + 1];
                        i++;
                } else if ((strcmp (argv[i], "-v") == 0) || (strcmp (argv[i], "--vid") == 0)) {
                        if (argc > (i + 1))
                                vid = strtoul( argv[i + 1], NULL, 16 );
                        i++;
                } else if ((strcmp (argv[i], "-p") == 0) || (strcmp (argv[i], "--pid") == 0)) {
                        if (argc > (i + 1))
                                pid = strtoul( argv[i + 1], NULL, 16 );
                        i++;
                } else {
                        fprintf (stderr, "Error: Unknown parameter %s\n", argv[i]);
                        fx2_dnld_print_usage (argv[0]);
                        return -EINVAL;
                }
	}
	if ((filename == NULL) || (tgt == FW_TARGET_NONE)) {
		fprintf (stderr, "Error: Firmware binary or target not specified\n");
		fx2_dnld_print_usage (argv[0]);
		return -EINVAL;
	}

	if ((vid && !pid) || (!vid && pid)) {
		fprintf (stderr, "Must provide BOTH VID and PID or leave BOTH blank to pick up from /etc/cyusb.conf\n");
		fx2_dnld_print_usage (argv[0]);
		return -EINVAL;
	}

	if ( vid && pid )
                r = cyusb_open (vid, pid);
        else
		r = cyusb_open ();
	if (r < 0) {
	     fprintf (stderr, "Error opening library\n");
	     return -ENODEV;
	}
	else if (r == 0) {
	        fprintf (stderr, "Error: No FX2LP device found\n");
		return -ENODEV;
	}
	else if (r > 1) {
		fprintf (stderr, "Error: More than one Cypress device found\n");
		return -EINVAL;
	}

	h = cyusb_gethandle(0);

	switch (tgt) {
		case FW_TARGET_RAM:
			/* Assuming an external RAM download for now. This works for
			   internal download as well. */
			r = fx2_ram_download (h, filename, 1);
			break;
		case FW_TARGET_SM_I2C:
			r = fx2_eeprom_download (h, filename, 0);
			break;
		case FW_TARGET_LR_I2C:
			r = fx2_eeprom_download (h, filename, 1);
			break;
		default:
			break;
	}
	if (r != 0) {
		fprintf (stderr, "Error: FX2LP firmware programming failed\n");
	} else {
		printf ("FX2LP firmware programming to %s completed\n", tgt_str);
	}

	cyusb_close ();
	return 0;
}
