FCN_FILE_DIRS += scripts/specfun

%canon_reldir%_FCN_FILES = \
  %reldir%/bessel.m \
  %reldir%/beta.m \
  %reldir%/betaln.m \
  %reldir%/ellipke.m \
  %reldir%/expint.m \
  %reldir%/factor.m \
  %reldir%/factorial.m \
  %reldir%/isprime.m \
  %reldir%/lcm.m \
  %reldir%/legendre.m \
  %reldir%/nchoosek.m \
  %reldir%/nthroot.m \
  %reldir%/perms.m \
  %reldir%/pow2.m \
  %reldir%/primes.m \
  %reldir%/reallog.m \
  %reldir%/realpow.m \
  %reldir%/realsqrt.m

%canon_reldir%dir = $(fcnfiledir)/specfun

%canon_reldir%_DATA = $(%canon_reldir%_FCN_FILES)

FCN_FILES += $(%canon_reldir%_FCN_FILES)

PKG_ADD_FILES += %reldir%/PKG_ADD

DIRSTAMP_FILES += %reldir%/$(octave_dirstamp)
