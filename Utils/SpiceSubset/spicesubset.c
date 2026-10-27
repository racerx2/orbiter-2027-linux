// not upstream: copies the SPK segments of some bodies, clipped to a time window, into a new SPK (Kernels/kerberos_styx.bsp)
// kerberos_styx.bsp: spicesubset Kernels/naif0012.tls plu060.bsp kerberos_styx.bsp "1900 JAN 01 00:00:00.000 TDB" "2200 JAN 01 00:00:00.000 TDB" 904 905
// usage: spicesubset <lsk> <in.bsp> <out.bsp> "<begin TDB>" "<end TDB>" <body> [<body> ...]
//        spicesubset -check <lsk> <a.bsp> <b.bsp> "<begin TDB>" "<end TDB>" <body> [<body> ...]

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "SpiceUsr.h"

static int Failed (void)
{
	if (!failed_c ()) return 0;
	char msg[1841];
	getmsg_c ("LONG", sizeof msg, msg);
	fprintf (stderr, "spicesubset: %s\n", msg);
	return 1;
}

static int Listed (SpiceInt body, int n, char **bodies)
{
	for (int i = 0; i < n; i++) if (atoi (bodies[i]) == body) return 1;
	return 0;
}

static int Copy (const char *in, const char *out, SpiceDouble et0, SpiceDouble et1, int nb, char **bodies, const char *cmd)
{
	SpiceInt hin, hout, nseg = 0;
	dafopr_c (in, &hin);
	spkopn_c (out, "spicesubset", 4096, &hout);
	if (Failed ()) return 1;
	SpiceBoolean found;
	dafbfs_c (hin);
	daffna_c (&found);
	while (found && !failed_c ()) {
		SpiceDouble sum[5], dc[2];
		SpiceInt ic[6];
		dafgs_c (sum);
		dafus_c (sum, 2, 6, dc, ic);
		SpiceDouble b = (dc[0] > et0 ? dc[0] : et0), e = (dc[1] < et1 ? dc[1] : et1);
		if (Listed (ic[0], nb, bodies) && b < e) {
			char ident[41];
			dafgn_c (sizeof ident, ident);
			spksub_c (hin, sum, ident, b, e, hout);
			printf ("segment %d: body %d centre %d frame %d type %d, %s\n", (int)++nseg, (int)ic[0], (int)ic[1], (int)ic[2], (int)ic[3], ident);
		}
		daffna_c (&found);
	}
	char note[3][80];
	snprintf (note[0], 80, "Made by Orbiter's Utils/SpiceSubset from %s:", in);
	snprintf (note[1], 80, "%s", cmd);
	snprintf (note[2], 80, "The data are JPL's, unchanged.");
	dafac_c (hout, 3, 80, note);
	spkcls_c (hout);
	dafcls_c (hin);
	if (Failed ()) return 1;
	printf ("%d segments written to %s\n", (int)nseg, out);
	return nseg ? 0 : 1;
}

// the same states from both files at 1,000 epochs spread over the window
static int Check (const char *a, const char *b, SpiceDouble et0, SpiceDouble et1, int nb, char **bodies)
{
	int bad = 0;
	for (int k = 0; k < nb; k++) {
		SpiceInt body = atoi (bodies[k]);
		double dmax = 0.0;
		for (int i = 0; i < 1000; i++) {
			SpiceDouble et = et0 + (et1 - et0)*(i + 0.5)/1000.0, sa[6], sb[6], lt;
			furnsh_c (a);
			spkgeo_c (body, et, "J2000", 9, sa, &lt);
			unload_c (a);
			furnsh_c (b);
			spkgeo_c (body, et, "J2000", 9, sb, &lt);
			unload_c (b);
			if (Failed ()) return 1;
			for (int j = 0; j < 6; j++) if (fabs (sa[j] - sb[j]) > dmax) dmax = fabs (sa[j] - sb[j]);
		}
		printf ("body %d: largest difference %g km (km/s)\n", (int)body, dmax);
		if (dmax != 0.0) bad = 1;
	}
	return bad;
}

int main (int argc, char **argv)
{
	int check = (argc > 1 && !strcmp (argv[1], "-check"));
	if (argc < 7 + check) {
		fprintf (stderr, "usage: %s [-check] <lsk> <in.bsp> <out.bsp> \"<begin TDB>\" \"<end TDB>\" <body> [<body> ...]\n", argv[0]);
		return 2;
	}
	char **arg = argv + 1 + check;
	erract_c ("SET", 0, "RETURN");
	errprt_c ("SET", 0, "NONE");
	furnsh_c (arg[0]);
	SpiceDouble et0, et1;
	str2et_c (arg[3], &et0);
	str2et_c (arg[4], &et1);
	if (Failed ()) return 1;
	if (check) return Check (arg[1], arg[2], et0, et1, argc - 6 - check, arg + 5);
	char cmd[80];
	snprintf (cmd, sizeof cmd, "bodies %s..., %s to %s", arg[5], arg[3], arg[4]);
	return Copy (arg[1], arg[2], et0, et1, argc - 6, arg + 5, cmd);
}
