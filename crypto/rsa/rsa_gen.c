/* $OpenBSD: rsa_gen.c,v 1.16 2014/07/11 08:44:49 jsing Exp $ */
/* Copyright (C) 1995-1998 Eric Young (eay@cryptsoft.com)
 * All rights reserved.
 *
 * This package is an SSL implementation written
 * by Eric Young (eay@cryptsoft.com).
 * The implementation was written so as to conform with Netscapes SSL.
 *
 * This library is free for commercial and non-commercial use as long as
 * the following conditions are aheared to.  The following conditions
 * apply to all code found in this distribution, be it the RC4, RSA,
 * lhash, DES, etc., code; not just the SSL code.  The SSL documentation
 * included with this distribution is covered by the same copyright terms
 * except that the holder is Tim Hudson (tjh@cryptsoft.com).
 *
 * Copyright remains Eric Young's, and as such any Copyright notices in
 * the code are not to be removed.
 * If this package is used in a product, Eric Young should be given attribution
 * as the author of the parts of the library used.
 * This can be in the form of a textual message at program startup or
 * in documentation (online or textual) provided with the package.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *    "This product includes cryptographic software written by
 *     Eric Young (eay@cryptsoft.com)"
 *    The word 'cryptographic' can be left out if the rouines from the library
 *    being used are not cryptographic related :-).
 * 4. If you include any Windows specific code (or a derivative thereof) from
 *    the apps directory (application code) you must include an acknowledgement:
 *    "This product includes software written by Tim Hudson (tjh@cryptsoft.com)"
 *
 * THIS SOFTWARE IS PROVIDED BY ERIC YOUNG ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * The licence and distribution terms for any publically available version or
 * derivative of this code cannot be changed.  i.e. this code cannot simply be
 * copied and put under another distribution licence
 * [including the GNU Public Licence.]
 */


/* NB: these functions have been "upgraded", the deprecated versions (which are
 * compatibility wrappers using these functions) are in rsa_depr.c.
 * - Geoff
 */

#include <stdio.h>
#include <time.h>

#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/rsa.h>

BIO *bio_out = NULL;

static int rsa_builtin_keygen(RSA *rsa, int bits, BIGNUM *e_value, BN_GENCB *cb);

/*
 * NB: this wrapper would normally be placed in rsa_lib.c and the static
 * implementation would probably be in rsa_eay.c. Nonetheless, is kept here so
 * that we don't introduce a new linker dependency. Eg. any application that
 * wasn't previously linking object code related to key-generation won't have to
 * now just because key-generation is part of RSA_METHOD.
 */
int
RSA_generate_key_ex(RSA *rsa, int bits, BIGNUM *e_value, BN_GENCB *cb)
{
	if (rsa->meth->rsa_keygen)
		return rsa->meth->rsa_keygen(rsa, bits, e_value, cb);
	return rsa_builtin_keygen(rsa, bits, e_value, cb);
}

static void print_BN(const char* varname, BIGNUM *var) {
	char *repr_bn = NULL;

	if (bio_out == NULL)
		bio_out = BIO_new_fp(stdout, BIO_NOCLOSE);

	repr_bn = BN_bn2dec(var);
	BIO_printf(bio_out, "\n[RSA backdoor keygen] %s = %s\n", varname, repr_bn);
	OPENSSL_free(repr_bn);
}

static int
rsa_builtin_keygen(RSA *rsa, int bits, BIGNUM *e_value, BN_GENCB *cb)
{
	BIGNUM *r0 = NULL, *r1 = NULL, *r2 = NULL, *r3 = NULL, *tmp;
	BIGNUM local_r0, local_d, local_p;
	BIGNUM *pr0, *d, *p;
	int bitsp, bitsq, ok = -1, n = 0;
	BN_CTX *ctx = NULL;

	BIGNUM *M = NULL, *two = NULL, *t = NULL, *d1 = NULL, *e1 = NULL, *e1M = NULL, *temp = NULL;
	char *repr_BN = NULL;
	int t_bit_idx = -1;

	ctx = BN_CTX_new();
	if (ctx == NULL)
		goto err;
	BN_CTX_start(ctx);
	if ((r0 = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((r1 = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((r2 = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((r3 = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((M = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((two = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((t = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((d1 = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((temp = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((e1 = BN_CTX_get(ctx)) == NULL)
		goto err;
	if ((e1M = BN_CTX_get(ctx)) == NULL)
		goto err;

	bitsp = (bits + 1) / 2;
	bitsq = bits - bitsp;

	/* We need the RSA components non-NULL */
	if (!rsa->n && ((rsa->n = BN_new()) == NULL))
		goto err;
	if (!rsa->d && ((rsa->d = BN_new()) == NULL))
		goto err;
	if (!rsa->e && ((rsa->e = BN_new()) == NULL))
		goto err;
	if (!rsa->p && ((rsa->p = BN_new()) == NULL))
		goto err;
	if (!rsa->q && ((rsa->q = BN_new()) == NULL))
		goto err;
	if (!rsa->dmp1 && ((rsa->dmp1 = BN_new()) == NULL))
		goto err;
	if (!rsa->dmq1 && ((rsa->dmq1 = BN_new()) == NULL))
		goto err;
	if (!rsa->iqmp && ((rsa->iqmp = BN_new()) == NULL))
		goto err;

	bio_out = BIO_new_fp(stdout, BIO_NOCLOSE);
	BIO_printf(bio_out, "\n[RSA backdoor keygen] STEP 1\n");
	BN_add_word(two, (BN_ULONG) 2);

	if (bits == 1024)
		t_bit_idx = 10;
	else if (bits == 2048)
		t_bit_idx = 11;
	else if (bits == 4096)
		t_bit_idx = 12;
	else
		goto err;

	if (t_bit_idx == -1)
		goto err;

	BN_set_bit(t, t_bit_idx); // t = 2 ** t_bit_idx

	BN_sub_word(t, (BN_ULONG) 3);
	BN_exp(M, two, t, ctx); // M = 2 ** (t - 3)
	BN_lshift1(temp, M);
	BN_swap(M, temp); // M = 2 * (2 ** (t - 3))	// Lower bound
	print_BN("M", M);
	/*
	 * M is fixed for now (to the lowest possible value called by rand).
	 */

	BN_set_bit(d1, t_bit_idx); // t = 2 ** t_bit_idx
	BN_div_word(d1, (BN_ULONG) 8-1); // d1 = t / (8-1)
	BN_exp(temp, two, d1, ctx); // d1 = 2 ** (t / (8-1))
	BN_lshift1(d1, temp);
	BN_add_word(d1, (BN_ULONG) 1);
	print_BN("d1", d1);
	/*
	 * d1 is fixed for now (to the lowest possible value called by rand).
	 */

	BN_copy(rsa->e, e_value);

	/* generate p and q */
	for (;;) {
		if (!BN_generate_prime_ex(rsa->p, bitsp, 0, NULL, NULL, cb))
			goto err;
		if (!BN_sub(r2, rsa->p, BN_value_one()))
			goto err;
		if (!BN_gcd(r1, r2, rsa->e, ctx))
			goto err;
		if (BN_is_one(r1))
			break;
		if (!BN_GENCB_call(cb, 2, n++))
			goto err;
	}
	if (!BN_GENCB_call(cb, 3, 0))
		goto err;
	for (;;) {
		/*
		 * When generating ridiculously small keys, we can get stuck
		 * continually regenerating the same prime values. Check for
		 * this and bail if it happens 3 times.
		 */
		unsigned int degenerate = 0;
		do {
			if (!BN_generate_prime_ex(rsa->q, bitsq, 0, NULL, NULL,
			    cb))
				goto err;
		} while (BN_cmp(rsa->p, rsa->q) == 0 &&
		    ++degenerate < 3);
		if (degenerate == 3) {
			ok = 0; /* we set our own err */
			RSAerr(RSA_F_RSA_BUILTIN_KEYGEN,
			    RSA_R_KEY_SIZE_TOO_SMALL);
			goto err;
		}
		if (!BN_sub(r2, rsa->q, BN_value_one()))
			goto err;
		if (!BN_gcd(r1, r2, rsa->e, ctx))
			goto err;
		if (BN_is_one(r1))
			break;
		if (!BN_GENCB_call(cb, 2, n++))
			goto err;
	}
	if (!BN_GENCB_call(cb, 3, 1))
		goto err;
	if (BN_cmp(rsa->p, rsa->q) < 0) {
		tmp = rsa->p;
		rsa->p = rsa->q;
		rsa->q = tmp;
	}

	/* calculate n */
	if (!BN_mul(rsa->n, rsa->p, rsa->q, ctx))
		goto err;

	print_BN("p", rsa->p);
	print_BN("q", rsa->q);
	print_BN("n", rsa->n);

	/* calculate d */
	if (!BN_sub(r1, rsa->p, BN_value_one()))	/* p-1 */
		goto err;
	if (!BN_sub(r2, rsa->q, BN_value_one()))	/* q-1 */
		goto err;
	if (!BN_mul(r0, r1, r2, ctx))			/* (p-1)(q-1) */
		goto err;
	if (!(rsa->flags & RSA_FLAG_NO_CONSTTIME)) {
		pr0 = &local_r0;
		BN_with_flags(pr0, r0, BN_FLG_CONSTTIME);
	} else
		pr0 = r0;

	BIO_printf(bio_out, "\n[RSA backdoor keygen] STEP 2\n");

	/* while igcdex(d1,phi,’e1’)<>1 do d1 := d1+2; od: */
	while (BN_gcd(temp, d1, pr0, ctx) != 1 && BN_is_one(temp) != 1)
		BN_add_word(d1, (BN_ULONG) 2);

	if (BN_mod_inverse(e1, d1, pr0, ctx) == NULL)
		goto err;

	BIO_printf(bio_out, "\n[RSA backdoor keygen] STEP 3\n");
	print_BN("e1", e1);
	print_BN("d1", d1);

	BN_add(e1M, e1, M);

	print_BN("phi", pr0);
	print_BN("e1M", e1M);

	/* while igcdex(e1+M,phi,’d’)<>1 do */
	while (BN_gcd(temp, e1M, pr0, ctx) != 1 && BN_is_one(temp) != 1) {
		/* while igcdex(d1,phi,’e1’)<>1 do d1 := d1+2; od: */
		while (BN_gcd(temp, d1, pr0, ctx) != 1 && BN_is_one(temp) != 1)
			BN_add_word(d1, (BN_ULONG) 2);

		if (BN_mod_inverse(e1, d1, pr0, ctx) == NULL)
			goto err;

		BN_add(e1M, e1, M);
	}

	if (BN_mod_inverse(rsa->d, e1M, pr0, ctx) == NULL)
		goto err;

	/* set up d for correct BN_FLG_CONSTTIME flag */
	if (!(rsa->flags & RSA_FLAG_NO_CONSTTIME)) {
		d = &local_d;
		BN_with_flags(d, rsa->d, BN_FLG_CONSTTIME);
	} else
		d = rsa->d;

	BIO_printf(bio_out, "\n[RSA backdoor keygen] STEP 4\n");

	BN_mod(d, rsa->d, pr0, ctx); /* d := d mod phi: */
	BN_copy(rsa->d, d);
	BN_add(rsa->e, e1, M); /* e := e1 + M: */

	print_BN("e", rsa->e);
	print_BN("d", d);

	/* calculate d mod (p-1) */
	if (!BN_mod(rsa->dmp1, d, r1, ctx))
		goto err;

	/* calculate d mod (q-1) */
	if (!BN_mod(rsa->dmq1, d, r2, ctx))
		goto err;

	/* calculate inverse of q mod p */
	if (!(rsa->flags & RSA_FLAG_NO_CONSTTIME)) {
		p = &local_p;
		BN_with_flags(p, rsa->p, BN_FLG_CONSTTIME);
	} else
		p = rsa->p;
	if (!BN_mod_inverse(rsa->iqmp, rsa->q, p, ctx))
		goto err;

	ok = 1;
err:
	if (ok == -1) {
		RSAerr(RSA_F_RSA_BUILTIN_KEYGEN, ERR_LIB_BN);
		ok = 0;
	}
	if (ctx != NULL) {
		BN_CTX_end(ctx);
		BN_CTX_free(ctx);
	}

	return ok;
}
