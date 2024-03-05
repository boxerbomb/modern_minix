#include "../include/signal.h"
#include "errno.h"
/*#include "../include/setjmp.h"*/
#include "../include/sh.h"
#include "../include/stat.h"


/* -------- sh.c -------- */
/*
 * shell
 */

/* #include "sh.h" */

int	intr;
int	inparse;
char	flags['z'-'a'+1];
char	*flag = flags-'a';
char	*elinep = line+sizeof(line)-5;
char	*null	= "";
int	inword	=1;
struct	env	e ={line, iostack, iostack-1, NULL, FDBASE, NULL};

char	**environ;	/* environment pointer */

/*
 * default shell, search rules
 */
char	shellname[] = "/bin/sh";
char	search[] = ":/bin:/usr/bin";

int	(*qflag)() = SIG_IGN;

main(argc, argv)
int argc;
register char **argv;
{
	register int f;
	register char *s;
	int cflag;
	char *name, **ap;
	int (*iof)();

	if ((ap = environ) != NULL) {
		while (*ap)
			assign(*ap++, !COPYV);
		for (ap = environ; *ap;)
			export(lookup(*ap++));
	}
	closeall();
	areanum = 1;

	shell = lookup("SHELL");
	if (shell->value == null)
		setval(shell, shellname);
	export(shell);

	homedir = lookup("HOME");
	if (homedir->value == null)
		setval(homedir, "/");
	export(homedir);

	setval(lookup("$"), itoa(getpid(), 5));

	path = lookup("PATH");
	if (path->value == null)
		setval(path, search);
	export(path);

	ifs = lookup("IFS");
	if (ifs->value == null)
		setval(ifs, " \t\n");

	prompt = lookup("PS1");
	if (prompt->value == null)
#ifndef UNIXSHELL
		setval(prompt, "$ ");
#else
		setval(prompt, "% ");
#endif
	if (geteuid() == 0) {
		setval(prompt, "# ");
		prompt->status &= ~EXPORT;
	}
	cprompt = lookup("PS2");
	if (cprompt->value == null)
		setval(cprompt, "> ");

	iof = filechar;
	cflag = 0;
	name = *argv++;
	if (--argc >= 1) {
		if(argv[0][0] == '-' && argv[0][1] != '\0') {
			for (s = argv[0]+1; *s; s++)
				switch (*s) {
				case 'c':
					prompt->status &= ~EXPORT;
					cprompt->status &= ~EXPORT;
					setval(prompt, "");
					setval(cprompt, "");
					cflag = 1;
					if (--argc > 0)
						PUSHIO(aword, *++argv, iof = nlchar);
					break;
	
				case 'q':
					qflag = SIG_DFL;
					break;

				case 's':
					/* standard input */
					break;

				case 't':
					prompt->status &= ~EXPORT;
					setval(prompt, "");
					iof = linechar;
					break;
	
				case 'i':
					talking++;
				default:
					if (*s>='a' && *s<='z')
						flag[*s]++;
				}
		} else {
			argv--;
			argc++;
		}
		if (iof == filechar && --argc > 0) {
			setval(prompt, "");
			setval(cprompt, "");
			prompt->status &= ~EXPORT;
			cprompt->status &= ~EXPORT;
			if (newfile(*++argv))
				exit(1);
		}
	}
	setdash();
	if (e.iop < iostack) {
		PUSHIO(afile, 0, iof);
		if (isatty(0) && isatty(1) && !cflag)
			talking++;
	}
	signal(SIGQUIT, qflag);
	if (name[0] == '-') {
		talking++;
		if ((f = open("/etc/profile", 0)) >= 0)
			next(remap(f));
		if ((f = open(".profile", 0)) >= 0)
			next(remap(f));
	}
	if (talking) {
		signal(SIGTERM, sig);
		signal(SIGINT, SIG_IGN);
	}
	dolv = argv;
	dolc = argc;
	dolv[0] = name;
	if (dolc > 1)
		for (ap = ++argv; --argc > 0;)
			if (assign(*ap = *argv++, !COPYV))
				dolc--;	/* keyword */
			else
				ap++;
	setval(lookup("#"), putn(dolc));

	for (;;) {
		if (talking && e.iop <= iostack)
			prs(prompt->value);
		onecommand();
	}
}

setdash()
{
	register char *cp, c;
	char m['z'-'a'+1];

	cp = m;
	for (c='a'; c<='z'; c++)
		if (flag[c])
			*cp++ = c;
	*cp = 0;
	setval(lookup("-"), m);
}

newfile(s)
register char *s;
{
	register f;

	if (strcmp(s, "-") != 0) {
		f = open(s, 0);
		if (f < 0) {
			prs(s);
			err(": cannot open");
			return(1);
		}
	} else
		f = 0;
	next(remap(f));
	return(0);
}

onecommand()
{
	register char *cp;
	register i;
	jmp_buf m1;

	inword++;
	while (e.oenv)
		quitenv();
	freearea(areanum = 1);
	cp = getcell(258);
	garbage();
	DELETE(cp);
	wdlist = 0;
	iolist = 0;
	e.errpt = 0;
	e.linep = line;
	yynerrs = 0;
	multiline = 0;
	inparse = 1;
	if (talking)
		signal(SIGINT, onintr);
	if (setjmp(failpt = m1) || yyparse() || intr) {
		while (e.oenv)
			quitenv();
		scraphere();
		inparse = 0;
		intr = 0;
		return;
	}
	inparse = 0;
	inword = 0;
	if ((i = trapset) != 0) {
		trapset = 0;
		runtrap(i);
	}
	brklist = 0;
	intr = 0;
	execflg = 0;
	if (!flag['n']) {
		if (talking)
			signal(SIGINT, onintr);
		execute(outtree, NOPIPE, NOPIPE, 0);
		intr = 0;
		if (talking)
			signal(SIGINT, SIG_IGN);
	}
}

void
fail()
{
	longjmp(failpt, 1);
	/* NOTREACHED */
}

void
leave()
{
	if (execflg)
		fail();
	runtrap(0);
	sync();
	exit(exstat);
	/* NOTREACHED */
}

warn(s)
register char *s;
{
	if(*s) {
		prs(s);
		exstat = -1;
	}
	prs("\n");
	if (flag['e'])
		leave();
}

err(s)
char *s;
{
	warn(s);
	if (flag['n'])
		return;
	if (!talking)
		leave();
	if (e.errpt)
		longjmp(e.errpt, 1);
	closeall();
	e.iop = e.iobase = iostack;
}

newenv(f)
{
	register struct env *ep;

	if (f) {
		quitenv();
		return(1);
	}
	ep = (struct env *) space(sizeof(*ep));
	if (ep == NULL) {
		while (e.oenv)
			quitenv();
		fail();
	}
	*ep = e;
	e.oenv = ep;
	e.errpt = errpt;
	return(0);
}

quitenv()
{
	register struct env *ep;
	register fd;

	if ((ep = e.oenv) != NULL) {
		fd = e.iofd;
		e = *ep;
		/* should close `'d files */
		DELETE(ep);
		while (--fd >= e.iofd)
			close(fd);
	}
}

/*
 * Is any character from s1 in s2?
 */
int
anys(s1, s2)
register char *s1, *s2;
{
	while (*s1)
		if (any(*s1++, s2))
			return(1);
	return(0);
}

/*
 * Is character c in s?
 */
int
any(c, s)
register int c;
register char *s;
{
	while (*s)
		if (*s++ == c)
			return(1);
	return(0);
}

char *
putn(n)
register n;
{
	return(itoa(n, -1));
}

char *
itoa(u, n)
register unsigned u;
{
	register char *cp;
	static char s[20];
	int m;

	m = 0;
	if (n < 0 && (int) u < 0) {
		m++;
		u = -u;
	}
	cp = s+sizeof(s);
	*--cp = 0;
	do {
		*--cp = u%10 + '0';
		u /= 10;
	} while (--n > 0 || u);
	if (m)
		*--cp = '-';
	return(cp);
}

next(f)
{
	PUSHIO(afile, f, nextchar);
}

onintr()
{
	signal(SIGINT, SIG_IGN);
	if (inparse) {
		prs("\n");
		fail();
	}
	intr++;
}

letter(c)
register c;
{
	return(c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z' || c == '_');
}

digit(c)
register c;
{
	return(c >= '0' && c <= '9');
}

letnum(c)
register c;
{
	return(letter(c) || digit(c));
}

char *
space(n)
int n;
{
	register char *cp;

	inword++;
	if ((cp = getcell(n)) == 0)
		err("out of string space");
	inword--;
	return(cp);
}

char *
strsave(s, a)
register char *s;
{
	register char *cp, *xp;

	if ((cp = space(strlen(s)+1)) != NULL) {
		setarea((char *)cp, a);
		for (xp = cp; (*xp++ = *s++) != '\0';)
			;
		return(cp);
	}
	return("");
}

/*
 * if inword is set, traps
 * are delayed, avoiding
 * having two people allocating
 * at once.
 */
xfree(s)
register char *s;
{
	inword++;
	DELETE(s);
	inword--;
}

/*
 * trap handling
 */
sig(i)
register i;
{
	if (inword == 0) {
		signal(i, SIG_IGN);
		runtrap(i);
	} else
		trapset = i;
	signal(i, sig);
}

runtrap(i)
{
	char *trapstr;

	if ((trapstr = trap[i]) == NULL)
		return;
	if (i == 0)
		trap[i] = 0;
	RUN(aword, trapstr, nlchar);
}

/* -------- var.c -------- */
/* #include "sh.h" */

static	char	*findeq();

/*
 * Find the given name in the dictionary
 * and return its value.  If the name was
 * not previously there, enter it now and
 * return a null value.
 */
struct var *
lookup(n)
register char *n;
{
	register struct var *vp;
	register char *cp;
	register int c;
	static struct var dummy;

	if (digit(*n)) {
		dummy.name = n;
		for (c = 0; digit(*n) && c < 1000; n++)
			c = c*10 + *n-'0';
		dummy.status = RONLY;
		dummy.value = c <= dolc? dolv[c]: null;
		return(&dummy);
	}
	for (vp = vlist; vp; vp = vp->next)
		if (eqname(vp->name, n))
			return(vp);
	cp = findeq(n);
	vp = (struct var *)space(sizeof(*vp));
	if (vp == 0 || (vp->name = space(cp-n+2)) == 0) {
		dummy.name = dummy.value = "";
		return(&dummy);
	}
	for (cp = vp->name; (*cp = *n++) && *cp != '='; cp++)
		;
	if (*cp == 0)
		*cp = '=';
	*++cp = 0;
	setarea((char *)vp, 0);
	setarea((char *)vp->name, 0);
	vp->value = null;
	vp->next = vlist;
	vp->status = GETCELL;
	vlist = vp;
	return(vp);
}

/*
 * give variable at `vp' the value `val'.
 */
void
setval(vp, val)
struct var *vp;
char *val;
{
	nameval(vp, val, (char *)NULL);
}

/*
 * if name is not NULL, it must be
 * a prefix of the space `val',
 * and end with `='.
 * this is all so that exporting
 * values is reasonably painless.
 */
void
nameval(vp, val, name)
register struct var *vp;
char *val, *name;
{
	register char *cp, *xp;
	char *nv;
	int fl;

	if (vp->status & RONLY) {
		for (xp = vp->name; *xp && *xp != '=';)
			putc(*xp++);
		err(" is read-only");
		return;
	}
	fl = 0;
	if (name == NULL) {
		xp = space(strlen(vp->name)+strlen(val)+2);
		if (xp == 0)
			return;
		/* make string:  name=value */
		setarea((char *)xp, 0);
		name = xp;
		for (cp = vp->name; (*xp = *cp++) && *xp!='='; xp++)
			;
		if (*xp++ == 0)
			xp[-1] = '=';
		nv = xp;
		for (cp = val; (*xp++ = *cp++) != '\0';)
			;
		val = nv;
		fl = GETCELL;
	}
	if (vp->status & GETCELL)
		xfree(vp->name);	/* form new string `name=value' */
	vp->name = name;
	vp->value = val;
	vp->status |= fl;
}

void
export(vp)
struct var *vp;
{
	vp->status |= EXPORT;
}

void
ronly(vp)
struct var *vp;
{
	if (letter(vp->name[0]))	/* not an internal symbol ($# etc) */
		vp->status |= RONLY;
}

int
isassign(s)
register char *s;
{
	if (!letter(*s))
		return(0);
	for (; *s != '='; s++)
		if (*s == 0 || !letnum(*s))
			return(0);
	return(1);
}

int
assign(s, cf)
register char *s;
int cf;
{
	register char *cp;
	struct var *vp;

	if (!letter(*s))
		return(0);
	for (cp = s; *cp != '='; cp++)
		if (*cp == 0 || !letnum(*cp))
			return(0);
	vp = lookup(s);
	nameval(vp, ++cp, cf == COPYV? NULL: s);
	if (cf != COPYV)
		vp->status &= ~GETCELL;
	return(1);
}

int
checkname(cp)
register char *cp;
{
	if (!letter(*cp++))
		return(0);
	while (*cp)
		if (!letnum(*cp++))
			return(0);
	return(1);
}

void
putvlist(f, out)
register int f, out;
{
	register struct var *vp;

	for (vp = vlist; vp; vp = vp->next)
		if (vp->status & f && letter(*vp->name)) {
			if (vp->status & EXPORT)
				write(out, "export ", 7);
			if (vp->status & RONLY)
				write(out, "readonly ", 9);
			write(out, vp->name, findeq(vp->name) - vp->name);
			write(out, "\n", 1);
		}
}

int
eqname(n1, n2)
register char *n1, *n2;
{
	for (; *n1 != '=' && *n1 != 0; n1++)
		if (*n2++ != *n1)
			return(0);
	return(*n2 == 0 || *n2 == '=');
}

static char *
findeq(cp)
register char *cp;
{
	while (*cp != '\0' && *cp != '=')
		cp++;
	return(cp);
}

/* -------- gmatch.c -------- */
/*
 * int gmatch(string, pattern)
 * char *string, *pattern;
 *
 * Match a pattern as in sh(1).
 */

#define	NULL	0
#define	CMASK	0377
#define	QUOTE	0200
#define	QMASK	(CMASK&~QUOTE)
#define	NOT	'!'	/* might use ^ */

static	char	*cclass();

int
gmatch(s, p)
register char *s, *p;
{
	register int sc, pc;

	if (s == NULL || p == NULL)
		return(0);
	while ((pc = *p++ & CMASK) != '\0') {
		sc = *s++ & QMASK;
		switch (pc) {
		case '[':
			if ((p = cclass(p, sc)) == NULL)
				return(0);
			break;

		case '?':
			if (sc == 0)
				return(0);
			break;

		case '*':
			s--;
			do {
				if (*p == '\0' || gmatch(s, p))
					return(1);
			} while (*s++ != '\0');
			return(0);

		default:
			if (sc != (pc&~QUOTE))
				return(0);
		}
	}
	return(*s == 0);
}

static char *
cclass(p, sub)
register char *p;
register int sub;
{
	register int c, d, not, found;

	if ((not = *p == NOT) != 0)
		p++;
	found = not;
	do {
		if (*p == '\0')
			return(NULL);
		c = *p & CMASK;
		if (p[1] == '-' && p[2] != ']') {
			d = p[2] & CMASK;
			p++;
		} else
			d = c;
		if (c == sub || c <= sub && sub <= d)
			found = !not;
	} while (*++p != ']');
	return(found? p+1: NULL);
}

/* -------- area.c -------- */
#define GROWBY	1024
#define SHRINKBY	256
#define FREE 32767
#define BUSY 0
#define	ALIGN (sizeof(int)-1)

/* #include "area.h" */
#define	NULL	0

struct region {
	struct	region *next;
	int	area;
};

/*initial empty arena*/

/*
OLD LINE:
extern	struct region area1;
static	struct region area2 = {&area1, BUSY};
static	struct region area1 = {&area2, BUSY};
static	struct region *areap = &area1;
static	struct region *areatop = &area1;
static	struct region *areabrk;
*/


// Changed to this!!
/*
ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ
ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ
ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ
*/
// I am afraid that this might cause some issues in the connection to area1 and area2
static	struct region area1;
static	struct region area2 = {&area1, BUSY};
static	struct region area1 = {&area2, BUSY};
static	struct region *areap = &area1;
static	struct region *areatop = &area1;
static	struct region *areabrk;
char	*sbrk();
char	*brk();

char *
getcell(nbytes)
unsigned nbytes;
{
	register int rbytes;
	register struct region *p, *q;
	struct region *newbrk;

	if (areabrk == NULL)
		areabrk = (struct region *)(((int)sbrk(0)+ALIGN)&~ALIGN);

	rbytes = (nbytes+sizeof(struct region)-1)/sizeof(struct region) + 1;
	p=areap;
	for (;;) {
		do {
			if (p->area > areanum) {
				while ((q = p->next)->area > areanum)
					p->next = q->next;
				if (q >= p+rbytes) {
					areap = p+rbytes;
					if (q > areap) {
						areap->next = p->next;
						areap->area = FREE;
					}
					p->next = areap;
					p->area = areanum;
					return((char *)(p+1));
				}
			}
			q = p; p = p->next;
		} while (q >= areap || p < areap);
		newbrk = (struct region *)sbrk(rbytes>=GROWBY? rbytes: GROWBY);
		if ((int)newbrk == -1)
			return(NULL);
		newbrk = (struct region *)sbrk(0);
		areatop->next = areabrk;
		areatop->area = ((q=areabrk)!=areatop+1)? BUSY: FREE;
		areatop = areabrk->next = newbrk-1;
		areabrk->area = FREE; areabrk = newbrk;
		areatop->next = &area2; areatop->area=BUSY;
	}
}

void
freecell(cp)
char *cp;
{
	register struct region *p;

	if ((p = (struct region *)cp) != NULL) {
		p--;
		if (p < areap)
			areap = p;
		p->area = FREE;
	}
}

void
freearea(a)
register int a;
{
	register struct region *p, *top;

	top = areatop;
	p = &area1;
	do {
		if (p->area >= a)
			p->area = FREE;
		p = p->next;
	} while (p != top);
}

void
setarea(cp,a)
char *cp;
int a;
{
	register struct region *p;

	if ((p = (struct region *)cp) != NULL)
		(p-1)->area = a;
}

void
garbage()
{
	register struct region *p, *q, *top;
	register int nu;

	top = areatop;

	areap = p = &area1;
	do {
		if (p->area>areanum) {
			while ((q = p->next)->area > areanum)
				p->next = q->next;
			areap = p;
		}
		q = p; p = p->next;
	} while (p != top);
#if STOPSHRINK == 0
	nu = (SHRINKBY+sizeof(struct region)-1)/sizeof(struct region);
	if (areatop >= q+nu &&
	    q->area > areanum) {
		brk((char *)(areabrk -= nu));
		q->next = areatop = areabrk-1;
		areatop->next = &area2;
		areatop->area = BUSY;
	}
#endif
}


// sh2.c
typedef union {
	char	*cp;
	char	**wp;
	int	i;
	struct	op *o;
} YYSTYPE;
#define	WORD	256
#define	LOGAND	257
#define	LOGOR	258
#define	BREAK	259
#define	IF	260
#define	THEN	261
#define	ELSE	262
#define	ELIF	263
#define	FI	264
#define	CASE	265
#define	ESAC	266
#define	FOR	267
#define	WHILE	268
#define	UNTIL	269
#define	DO	270
#define	DONE	271
#define	IN	272
#define	YYERRCODE 300

/* flags to yylex */
#define	CONTIN	01	/* skip new lines to complete command */

/* #include "sh.h" */
#define	SYNTAXERR	zzerr()
static	int	startl = 1;
static	int	peeksym = 0;
static	void	zzerr();
static	void	word();
static	char	**copyw();
static	struct	op *block(), *namelist(), *list(), *newtp();
static	struct	op *pipeline(), *andor(), *command();
static	struct	op *nested(), *simple(), *c_list();
static	struct	op *dogroup(), *thenpart(), *casepart(), *caselist();
static	struct	op *elsepart();
static	char	**wordlist(), **pattern();
static	void	musthave();
static	int	yylex();
static	struct ioword *io();
static	struct ioword **copyio();
static	char	*tree();
static	void	diag();
static	int	nlseen;
static	int	iounit = IODEFAULT;
static	struct	op	*tp;
struct	op	*newtp();

static	YYSTYPE	yylval;

int
yyparse()
{
	peeksym = 0;
	yynerrs = 0;
	outtree = c_list();
	musthave('\n', 0);
	return(yynerrs!=0);
}

static struct op *
pipeline(cf)
int cf;
{
	register struct op *t, *p;
	register int c;

	t = command(cf);
	if (t != NULL) {
		while ((c = yylex(0)) == '|') {
			if ((p = command(CONTIN)) == NULL)
				SYNTAXERR;
			if (t->type != TPAREN && t->type != TCOM) {
				/* shell statement */
				t = block(TPAREN, t, NOBLOCK, NOWORDS);
			}
			t = block(TPIPE, t, p, NOWORDS);
		}
		peeksym = c;
	}
	return(t);
}

static struct op *
andor()
{
	register struct op *t, *p;
	register int c;

	t = pipeline(0);
	if (t != NULL) {
		while ((c = yylex(0)) == LOGAND || c == LOGOR) {
			if ((p = pipeline(CONTIN)) == NULL)
				SYNTAXERR;
			t = block(c == LOGAND? TAND: TOR, t, p, NOWORDS);
		}
		peeksym = c;
	}
	return(t);
}

static struct op *
c_list()
{
	register struct op *t, *p;
	register int c;

	t = andor();
	if (t != NULL) {
		while ((c = yylex(0)) == ';' || c == '&' || multiline && c == '\n') {
			if (c == '&')
				t = block(TASYNC, t, NOBLOCK, NOWORDS);
			if ((p = andor()) == NULL)
				return(t);
			t = list(t, p);
		}
		peeksym = c;
	}
	return(t);
}

static int
synio(cf)
int cf;
{
	register struct ioword *iop;
	register int i;
	register int c;

	if ((c = yylex(cf)) != '<' && c != '>') {
		peeksym = c;
		return(0);
	}
	i = yylval.i;
	musthave(WORD, 0);
	iop = io(iounit, i, yylval.cp);
	iounit = IODEFAULT;
	if (i & IOHERE)
		markhere(yylval.cp, iop);
}

static void
musthave(c, cf)
int c, cf;
{
	if ((peeksym = yylex(cf)) != c)
		SYNTAXERR;
	peeksym = 0;
}

static struct op *
simple()
{
	register struct op *t;

	t = NULL;
	for (;;) {
		switch (peeksym = yylex(0)) {
		case '<':
		case '>':
			(void) synio(0);
			break;

		case WORD:
			if (t == NULL) {
				t = newtp();
				t->type = TCOM;
			}
			peeksym = 0;
			word(yylval.cp);
			break;

		default:
			return(t);
		}
	}
}

static struct op *
nested(type, mark)
int type, mark;
{
	register struct op *t;

	multiline++;
	t = c_list();
	musthave(mark, 0);
	multiline--;
	return(block(type, t, NOBLOCK, NOWORDS));
}

static struct op *
command(cf)
int cf;
{
	register struct ioword *io;
	register struct op *t;
	struct wdblock *iosave;
	register int c;

	iosave = iolist;
	iolist = NULL;
	if (multiline)
		cf |= CONTIN;
	while (synio(cf))
		cf = 0;
	switch (c = yylex(cf)) {
	default:
		peeksym = c;
		if ((t = simple()) == NULL) {
			if (iolist == NULL)
				return(NULL);
			t = newtp();
			t->type = TCOM;
		}
		break;

	case '(':
		t = nested(TPAREN, ')');
		break;

	case '{':
		t = nested(TBRACE, '}');
		break;

	case FOR:
		t = newtp();
		t->type = TFOR;
		musthave(WORD, 0);
		startl = 1;
		t->str = yylval.cp;
		multiline++;
		t->words = wordlist();
		if ((c = yylex(0)) != '\n' && c != ';')
			SYNTAXERR;
		t->left = dogroup(0);
		multiline--;
		break;

	case WHILE:
	case UNTIL:
		multiline++;
		t = newtp();
		t->type = c == WHILE? TWHILE: TUNTIL;
		t->left = c_list();
		t->right = dogroup(1);
		t->words = NULL;
		multiline--;
		break;

	case CASE:
		t = newtp();
		t->type = TCASE;
		musthave(WORD, 0);
		t->str = yylval.cp;
		startl++;
		multiline++;
		musthave(IN, CONTIN);
		startl++;
		t->left = caselist();
		musthave(ESAC, 0);
		multiline--;
		break;

	case IF:
		multiline++;
		t = newtp();
		t->type = TIF;
		t->left = c_list();
		t->right = thenpart();
		musthave(FI, 0);
		multiline--;
		break;
	}
	while (synio(0))
		;
	t = namelist(t);
	iolist = iosave;
	return(t);
}

static struct op *
dogroup(onlydone)
int onlydone;
{
	register int c;
	register struct op *list;

	c = yylex(CONTIN);
	if (c == DONE && onlydone)
		return(NULL);
	if (c != DO)
		SYNTAXERR;
	list = c_list();
	musthave(DONE, 0);
	return(list);
}

static struct op *
thenpart()
{
	register int c;
	register struct op *t;

	if ((c = yylex(0)) != THEN) {
		peeksym = c;
		return(NULL);
	}
	t = newtp();
	t->type = 0;
	t->left = c_list();
	if (t->left == NULL)
		SYNTAXERR;
	t->right = elsepart();
	return(t);
}

static struct op *
elsepart()
{
	register int c;
	register struct op *t;

	switch (c = yylex(0)) {
	case ELSE:
		if ((t = c_list()) == NULL)
			SYNTAXERR;
		return(t);

	case ELIF:
		t = newtp();
		t->type = TELIF;
		t->left = c_list();
		t->right = thenpart();
		return(t);

	default:
		peeksym = c;
		return(NULL);
	}
}

static struct op *
caselist()
{
	register struct op *t;
	register int c;

	t = NULL;
	while ((peeksym = yylex(CONTIN)) != ESAC)
		t = list(t, casepart());
	return(t);
}

static struct op *
casepart()
{
	register struct op *t;
	register int c;

	t = newtp();
	t->type = TPAT;
	t->words = pattern();
	musthave(')', 0);
	t->left = c_list();
	if ((peeksym = yylex(CONTIN)) != ESAC)
		musthave(BREAK, CONTIN);
	return(t);
}

static char **
pattern()
{
	register int c, cf;

	cf = CONTIN;
	do {
		musthave(WORD, cf);
		word(yylval.cp);
		cf = 0;
	} while ((c = yylex(0)) == '|');
	peeksym = c;
	word(NOWORD);
	return(copyw());
}

static char **
wordlist()
{
	register int c;

	if ((c = yylex(0)) != IN) {
		peeksym = c;
		return(NULL);
	}
	startl = 0;
	while ((c = yylex(0)) == WORD)
		word(yylval.cp);
	word(NOWORD);
	peeksym = c;
	return(copyw());
}

/*
 * supporting functions
 */
static struct op *
list(t1, t2)
register struct op *t1, *t2;
{
	if (t1 == NULL)
		return(t2);
	if (t2 == NULL)
		return(t1);
	return(block(TLIST, t1, t2, NOWORDS));
}

static struct op *
block(type, t1, t2, wp)
struct op *t1, *t2;
char **wp;
{
	register struct op *t;

	t = newtp();
	t->type = type;
	t->left = t1;
	t->right = t2;
	t->words = wp;
	return(t);
}

struct res {
	char	*r_name;
	int	r_val;
} restab[] = {
	"for",		FOR,
	"case",		CASE,
	"esac",		ESAC,
	"while",	WHILE,
	"do",		DO,
	"done",		DONE,
	"if",		IF,
	"in",		IN,
	"then",		THEN,
	"else",		ELSE,
	"elif",		ELIF,
	"until",	UNTIL,
	"fi",		FI,

	";;",		BREAK,
	"||",		LOGOR,
	"&&",		LOGAND,
	"{",		'{',
	"}",		'}',

	0,
};

rlookup(n)
register char *n;
{
	register struct res *rp;

	for (rp = restab; rp->r_name; rp++)
		if (strcmp(rp->r_name, n) == 0)
			return(rp->r_val);
	return(0);
}

static struct op *
newtp()
{
	register struct op *t;

	t = (struct op *)tree(sizeof(*t));
	t->type = 0;
	t->words = NULL;
	t->ioact = NULL;
	t->left = NULL;
	t->right = NULL;
	t->str = NULL;
	return(t);
}

static struct op *
namelist(t)
register struct op *t;
{
	if (iolist) {
		iolist = addword((char *)NULL, iolist);
		t->ioact = copyio();
	} else
		t->ioact = NULL;
	if (t->type != TCOM) {
		if (t->type != TPAREN && t->ioact != NULL) {
			t = block(TPAREN, t, NOBLOCK, NOWORDS);
			t->ioact = t->left->ioact;
			t->left->ioact = NULL;
		}
		return(t);
	}
	word(NOWORD);
	t->words = copyw();
	return(t);
}

static char **
copyw()
{
	register char **wd;

	wd = getwords(wdlist);
	wdlist = 0;
	return(wd);
}

static void
word(cp)
char *cp;
{
	wdlist = addword(cp, wdlist);
}

static struct ioword **
copyio()
{
	register struct ioword **iop;

	iop = (struct ioword **) getwords(iolist);
	iolist = 0;
	return(iop);
}

static struct ioword *
io(u, f, cp)
char *cp;
{
	register struct ioword *iop;

	iop = (struct ioword *) tree(sizeof(*iop));
	iop->io_unit = u;
	iop->io_flag = f;
	iop->io_un.io_name = cp;
	iolist = addword((char *)iop, iolist);
	return(iop);
}

static void
zzerr()
{
	yyerror("syntax error");
}

yyerror(s)
char *s;
{
	yynerrs++;
	if (talking) {
		if (multiline && nlseen)
			unget('\n');
		multiline = 0;
		while (yylex(0) != '\n')
			;
	}
	err(s);
	fail();
}

static int
yylex(cf)
int cf;
{
	register int c, c1;
	int atstart;

	if ((c = peeksym) > 0) {
		peeksym = 0;
		if (c == '\n')
			startl = 1;
		return(c);
	}
	nlseen = 0;
	e.linep = line;
	atstart = startl;
	startl = 0;
	yylval.i = 0;

loop:
	while ((c = getc(0)) == ' ' || c == '\t')
		;
	switch (c) {
	default:
		if (any(c, "0123456789")) {
			unget(c1 = getc(0));
			if (c1 == '<' || c1 == '>') {
				iounit = c - '0';
				goto loop;
			}
			*e.linep++ = c;
			c = c1;
		}
		break;

	case '#':
		while ((c = getc(0)) != 0 && c != '\n')
			;
		unget(c);
		goto loop;

	case 0:
		return(c);

	case '$':
		*e.linep++ = c;
		if ((c = getc(0)) == '{') {
			if ((c = collect(c, '}')) != '\0')
				return(c);
			goto pack;
		}
		break;

	case '`':
	case '\'':
	case '"':
		if ((c = collect(c, c)) != '\0')
			return(c);
		goto pack;

	case '|':
	case '&':
	case ';':
		if ((c1 = dual(c)) != '\0') {
			startl = 1;
			return(c1);
		}
		startl = 1;
		return(c);
	case '^':
		startl = 1;
		return('|');
	case '>':
	case '<':
		diag(c);
		return(c);

	case '\n':
		nlseen++;
		gethere();
		startl = 1;
		if (multiline || cf & CONTIN) {
			if (talking && e.iop <= iostack)
				prs(cprompt->value);
			if (cf & CONTIN)
				goto loop;
		}
		return(c);

	case '(':
	case ')':
		startl = 1;
		return(c);
	}

	unget(c);

pack:
	while ((c = getc(0)) != 0 && !any(c, "`$ '\"\t;&<>()|^\n"))
		if (e.linep >= elinep)
			err("word too long");
		else
			*e.linep++ = c;
	unget(c);
	if(any(c, "\"'`$"))
		goto loop;
	*e.linep++ = '\0';
	if (atstart && (c = rlookup(line))!=0) {
		startl = 1;
		return(c);
	}
	yylval.cp = strsave(line, areanum);
	return(WORD);
}

int
collect(c, c1)
register c, c1;
{
	char s[2];

	*e.linep++ = c;
	while ((c = getc(c1)) != c1) {
		if (c == 0) {
			unget(c);
			s[0] = c1;
			s[1] = 0;
			prs("no closing "); yyerror(s);
			return(YYERRCODE);
		}
		if (talking && c == '\n' && e.iop <= iostack)
			prs(cprompt->value);
		*e.linep++ = c;
	}
	*e.linep++ = c;
	return(0);
}

int
dual(c)
register c;
{
	char s[3];
	register char *cp = s;

	*cp++ = c;
	*cp++ = getc(0);
	*cp = 0;
	if ((c = rlookup(s)) == 0)
		unget(*--cp);
	return(c);
}

static void
diag(ec)
register int ec;
{
	register int c;

	c = getc(0);
	if (c == '>' || c == '<') {
		if (c != ec)
			zzerr();
		yylval.i = ec == '>'? IOWRITE|IOCAT: IOHERE;
		c = getc(0);
	} else
		yylval.i = ec == '>'? IOWRITE: IOREAD;
	if (c != '&' || yylval.i == IOHERE)
		unget(c);
	else
		yylval.i |= IODUP;
}

static char *
tree(size)
unsigned size;
{
	register char *t;

	if ((t = getcell(size)) == NULL) {
		prs("command line too complicated\n");
		fail();
		/* NOTREACHED */
	}
	return(t);
}

/* VARARGS1 */
/* ARGSUSED */
printf(s)	/* yyparse calls it */
char *s;
{
}

/* -------- exec.c -------- */
/* #include "sh.h" */

/*
 * execute tree
 */

static	char	*signame[] = {
	"Signal 0",
	"Hangup",
	NULL,	/* interrupt */
	"Quit",
	"Illegal instruction",
	"Trace/BPT trap",
	"abort",
	"EMT trap",
	"Floating exception",
	"Killed",
	"Bus error",
	"Memory fault",
	"Bad system call",
	NULL,	/* broken pipe */
	"Alarm clock",
	"Terminated",
};
#define	NSIGNAL (sizeof(signame)/sizeof(signame[0]))

static	struct	op *findcase();
static	void	brkset();
static	void	echo();
static	int	forkexec();
static	int	parent();

int
execute(t, pin, pout, act)
register struct op *t;
int *pin, *pout;
int act;
{
	register struct op *t1;
	int i, pv[2], rv, child, a;
	char *cp, **wp, **wp2;
	struct var *vp;
	struct brkcon bc;

	if (t == NULL)
		return(0);
	rv = 0;
	a = areanum++;
	wp = (wp2 = t->words) != NULL? eval(wp2, DOALL): NULL;

	switch(t->type) {
	case TPAREN:
	case TCOM:
		rv = forkexec(t, pin, pout, act, wp, &child);
		if (child) {
			exstat = rv;
			leave();
		}
		break;

	case TPIPE:
		if ((rv = openpipe(pv)) < 0)
			break;
		pv[0] = remap(pv[0]);
		pv[1] = remap(pv[1]);
		(void) execute(t->left, pin, pv, 0);
		rv = execute(t->right, pv, pout, 0);
		break;

	case TLIST:
		(void) execute(t->left, pin, pout, 0);
		rv = execute(t->right, pin, pout, 0);
		break;

	case TASYNC:
		i = parent();
		if (i != 0) {
			if (i != -1) {
				if (pin != NULL)
					closepipe(pin);
				if (talking) {
					prs(putn(i));
					prs("\n");
				}
			} else
				rv = -1;
			setstatus(rv);
		} else {
			signal(SIGINT, SIG_IGN);
			signal(SIGQUIT, SIG_IGN);
			if (talking)
				signal(SIGTERM, SIG_DFL);
			talking = 0;
			if (pin == NULL) {
				close(0);
				open("/dev/null", 0);
			}
			exit(execute(t->left, pin, pout, FEXEC));
		}
		break;

	case TOR:
	case TAND:
		rv = execute(t->left, pin, pout, 0);
		if ((t1 = t->right)!=NULL && (rv == 0) == (t->type == TAND))
			rv = execute(t1, pin, pout, 0);
		break;

	case TFOR:
		if (wp == NULL) {
			wp = dolv+1;
			if ((i = dolc-1) < 0)
				i = 0;
		} else
			i = -1;
		vp = lookup(t->str);
		while (setjmp(bc.brkpt))
			if (isbreak)
				goto broken;
		brkset(&bc);
		for (t1 = t->left; i-- && *wp != NULL;) {
			setval(vp, *wp++);
			rv = execute(t1, pin, pout, 0);
		}
		brklist = brklist->nextlev;
		break;

	case TWHILE:
	case TUNTIL:
		while (setjmp(bc.brkpt))
			if (isbreak)
				goto broken;
		brkset(&bc);
		t1 = t->left;
		while ((execute(t1, pin, pout, 0) == 0) == (t->type == TWHILE))
			rv = execute(t->right, pin, pout, 0);
		brklist = brklist->nextlev;
		break;

	case TIF:
	case TELIF:
		rv = !execute(t->left, pin, pout, 0)?
			execute(t->right->left, pin, pout, 0):
			execute(t->right->right, pin, pout, 0);
		break;

	case TCASE:
		if ((cp = evalstr(t->str, DOSUB|DOTRIM)) == 0)
			cp = "";
		if ((t1 = findcase(t->left, cp)) != NULL)
			rv = execute(t1, pin, pout, 0);
		break;

	case TBRACE:
/*
		if (iopp = t->ioact)
			while (*iopp)
				if (iosetup(*iopp++, pin!=NULL, pout!=NULL)) {
					rv = -1;
					break;
				}
*/
		if (rv >= 0 && (t1 = t->left))
			rv = execute(t1, pin, pout, 0);
		break;
	}

broken:
	t->words = wp2;
	isbreak = 0;
	freearea(areanum);
	areanum = a;
	if (intr) {
		closeall();
		fail();
	}
	return(rv);
}

static int
forkexec(t, pin, pout, act, wp, pforked)
register struct op *t;
int *pin, *pout;
int act;
char **wp;
int *pforked;
{
	int i, rv, (*shcom)();
	int doexec();
	register int f;
	char *cp;
	struct ioword **iopp;
	int resetsig;

	resetsig = 0;
	*pforked = 0;
	shcom = NULL;
	rv = -1;	/* system-detected error */
	if (t->type == TCOM) {
		/* strip all initial assignments */
		/* not correct wrt PATH=yyy command  etc */
		if (flag['x'])
			echo(wp);
		while ((cp = *wp++) != NULL && assign(cp, COPYV))
			;
		wp--;
		if (cp == NULL && t->ioact == NULL)
			return(setstatus(0));
		else
			shcom = inbuilt(cp);
	}
	t->words = wp;
	f = act;
	if (shcom == NULL && (f & FEXEC) == 0) {
		i = parent();
		if (i != 0) {
			if (i == -1)
				return(rv);
			if (pin != NULL)
				closepipe(pin);
			return(pout==NULL? setstatus(waitfor(i,0)): 0);
		}
		if (talking) {
			signal(SIGINT, SIG_IGN);
			signal(SIGQUIT, SIG_IGN);
			resetsig = 1;
		}
		talking = 0;
		intr = 0;
		(*pforked)++;
		brklist = 0;
		execflg = 0;
	}
#ifdef COMPIPE
	if ((pin != NULL || pout != NULL) && shcom != NULL && shcom != doexec) {
		err("piping to/from shell builtins not yet done");
		return(-1);
	}
#endif
	if (pin != NULL) {
		dup2(pin[0], 0);
		closepipe(pin);
	}
	if (pout != NULL) {
		dup2(pout[1], 1);
		closepipe(pout);
	}
	if ((iopp = t->ioact) != NULL) {
		if (shcom != NULL && shcom != doexec) {
			prs(cp);
			err(": cannot redirect shell command");
			return(-1);
		}
		while (*iopp)
			if (iosetup(*iopp++, pin!=NULL, pout!=NULL))
				return(rv);
	}
	if (shcom)
		return(setstatus((*shcom)(t)));
	/* should use FIOCEXCL */
	for (i=FDBASE; i<NOFILE; i++)
		close(i);
	if (t->type == TPAREN)
		exit(execute(t->left, NOPIPE, NOPIPE, FEXEC));
	if (resetsig) {
		signal(SIGINT, SIG_DFL);
		signal(SIGQUIT, SIG_DFL);
	}
	if (wp[0] == NULL)
		exit(0);
	cp = rexecve(wp[0], wp, makenv(wp));
	prs(wp[0]); prs(": "); warn(cp);
	if (!execflg)
		trap[0] = NULL;
	leave();
	/* NOTREACHED */
}

/*
 * common actions when creating a new child
 */
static int
parent()
{
	register int i;

	i = fork();
	if (i != 0) {
		if (i == -1)
			warn("try again");
		setval(lookup("!"), putn(i));
	}
	return(i);
}

/*
 * 0< 1> are ignored as required
 * within pipelines.
 */
iosetup(iop, pipein, pipeout)
register struct ioword *iop;
int pipein, pipeout;
{
	register u;
	char *cp, *msg;

	if (iop->io_unit == IODEFAULT)	/* take default */
		iop->io_unit = iop->io_flag&(IOREAD|IOHERE)? 0: 1;
	if (pipein && iop->io_unit == 0)
		return(0);
	if (pipeout && iop->io_unit == 1)
		return(0);
	msg = iop->io_flag&(IOREAD|IOHERE)? "open": "create";
	if ((iop->io_flag & IOHERE) == 0) {
		cp = iop->io_un.io_name;
		if ((cp = evalstr(cp, DOSUB|DOTRIM)) == NULL)
			return(1);
	}
	if (iop->io_flag & IODUP) {
		if (cp[1] || !digit(*cp) && *cp != '-') {
			prs(cp);
			err(": illegal >& argument");
			return(1);
		}
		if (*cp == '-')
			iop->io_flag = IOCLOSE;
		iop->io_flag &= ~(IOREAD|IOWRITE);
	}
	switch (iop->io_flag) {
	case IOREAD:
		u = open(cp, 0);
		break;

	case IOHERE:
	case IOHERE|IOXHERE:
		u = herein(iop->io_un.io_here, iop->io_flag&IOXHERE);
		cp = "here file";
		break;

	case IOWRITE|IOCAT:
		if ((u = open(cp, 1)) >= 0) {
			lseek(u, (long)0, 2);
			break;
		}
	case IOWRITE:
		u = creat(cp, 0666);
		break;

	case IODUP:
		u = dup2(*cp-'0', iop->io_unit);
		break;

	case IOCLOSE:
		close(iop->io_unit);
		return(0);
	}
	if (u < 0) {
		prs(cp);
		prs(": cannot ");
		warn(msg);
		return(1);
	} else {
		if (u != iop->io_unit) {
			dup2(u, iop->io_unit);
			close(u);
		}
	}
	return(0);
}

static void
echo(wp)
register char **wp;
{
	register i;

	prs("+");
	for (i=0; wp[i]; i++) {
		if (i)
			prs(" ");
		prs(wp[i]);
	}
	prs("\n");
}

static struct op **
find1case(t, w)
struct op *t;
char *w;
{
	register struct op *t1;
	struct op **tp;
	register char **wp, *cp;

	if (t == NULL)
		return(NULL);
	if (t->type == TLIST) {
		if ((tp = find1case(t->left, w)) != NULL)
			return(tp);
		t1 = t->right;	/* TPAT */
	} else
		t1 = t;
	for (wp = t1->words; *wp;)
		if ((cp = evalstr(*wp++, DOSUB)) && gmatch(w, cp))
			return(&t1->left);
	return(NULL);
}

static struct op *
findcase(t, w)
struct op *t;
char *w;
{
	register struct op **tp;

	return((tp = find1case(t, w)) != NULL? *tp: NULL);
}

/*
 * Enter a new loop level (marked for break/continue).
 */
static void
brkset(bc)
struct brkcon *bc;
{
	bc->nextlev = brklist;
	brklist = bc;
}

/*
 * Wait for the last process created.
 * Print a message for each process found
 * that was killed by a signal.
 * Ignore interrupt signals while waiting
 * unless `canintr' is true.
 */
int
waitfor(lastpid, canintr)
register int lastpid;
int canintr;
{
	register int pid, rv;
	int s;

	rv = 0;
	do {
		pid = wait(&s);
		if (pid == -1) {
			if (errno != EINTR || canintr)
				break;
		} else {
			if ((rv = WAITSIG(s)) != 0) {
				if (rv < NSIGNAL) {
					if (signame[rv] != NULL) {
						if (pid != lastpid) {
							prn(pid);
							prs(": ");
						}
						prs(signame[rv]);
					}
				} else {
					if (pid != lastpid) {
						prn(pid);
						prs(": ");
					}
					prs("Signal "); prn(rv); prs(" ");
				}
				if (WAITCORE(s))
					prs(" - core dumped");
				prs("\n");
				rv = -1;
			} else
				rv = WAITVAL(s);
		}
/* Special patch for MINIX: sync before each command */
		sync();
	} while (pid != lastpid);
	return(rv);
}

int
setstatus(s)
register int s;
{
	exstat = s;
	setval(lookup("?"), putn(s));
	return(s);
}

/*
 * PATH-searching interface to execve.
 * If getenv("PATH") were kept up-to-date,
 * execvp might be used.
 */
char *
rexecve(c, v, envp)
char *c, **v, **envp;
{
	register int i;
	register char *sp, *tp;
	int eacces = 0, asis = 0;
	extern int errno;

	sp = any('/', c)? "": path->value;
	asis = *sp == '\0';
	while (asis || *sp != '\0') {
		asis = 0;
		tp = e.linep;
		for (; *sp != '\0'; tp++)
			if ((*tp = *sp++) == ':') {
				asis = *sp == '\0';
				break;
			}
		if (tp != e.linep)
			*tp++ = '/';
		for (i = 0; (*tp++ = c[i++]) != '\0';)
			;
		execve(e.linep, v, envp);
		switch (errno) {
		case ENOEXEC:
			*v = e.linep;
			tp = *--v;
			*v = "/bin/sh";
			execve(*v, v, envp);
			*v = tp;
			return("no Shell");

		case ENOMEM:
			return("program too big");

		case E2BIG:
			return("argument list too long");

		case EACCES:
			eacces++;
			break;
		}
	}
	return(errno==ENOENT ? "not found" : "cannot execute");
}

/*
 * Run the command produced by generator `f'
 * applied to stream `arg'.
 */
run(arg, f)
struct ioarg arg;
int (*f)();
{
	struct op *otree;
	struct wdblock *swdlist;
	struct wdblock *siolist;
	jmp_buf ev, rt;
	xint *ofail;
	int rv;

	areanum++;
	swdlist = wdlist;
	siolist = iolist;
	otree = outtree;
	ofail = failpt;
	rv = -1;
	if (newenv(setjmp(errpt = ev)) == 0) {
		wdlist = 0;
		iolist = 0;
		pushio(arg, f);
		e.iobase = e.iop;
		yynerrs = 0;
		if (setjmp(failpt = rt) == 0 && yyparse() == 0)
			rv = execute(outtree, NOPIPE, NOPIPE, 0);
		quitenv();
	}
	wdlist = swdlist;
	iolist = siolist;
	failpt = ofail;
	outtree = otree;
	freearea(areanum--);
	return(rv);
}

/* -------- do.c -------- */
/* #include "sh.h" */

/*
 * built-in commands: doX
 */

static	void	rdexp();
static	void	badid();
static	int	brkcontin();

dolabel()
{
	return(0);
}

dochdir(t)
register struct op *t;
{
	register char *cp, *er;

	if ((cp = t->words[1]) == NULL && (cp = homedir->value) == NULL)
		er = ": no home directory";
	else if(chdir(cp) < 0)
		er = ": bad directory";
	else
		return(0);
	prs(cp != NULL? cp: "cd");
	err(er);
	return(1);
}

doshift(t)
register struct op *t;
{
	register n;

	n = t->words[1]? getn(t->words[1]): 1;
	if(dolc < n) {
		err("nothing to shift");
		return(1);
	}
	dolv[n] = dolv[0];
	dolv += n;
	dolc -= n;
	setval(lookup("#"), putn(dolc));
	return(0);
}

/*
 * execute login and newgrp directly
 */
dologin(t)
struct op *t;
{
	register char *cp;

	if (talking) {
		signal(SIGINT, SIG_DFL);
		signal(SIGQUIT, SIG_DFL);
	}
	cp = rexecve(t->words[0], t->words, makenv(t->words));
	prs(t->words[0]); prs(": "); err(cp);
	return(1);
}

doumask(t)
register struct op *t;
{
	register int i, n;
	register char *cp;

	if ((cp = t->words[1]) == NULL) {
		i = umask(0);
		umask(i);
		for (n=3*4; (n-=3) >= 0;)
			putc('0'+((i>>n)&07));
		putc('\n');
	} else {
		for (n=0; *cp>='0' && *cp<='9'; cp++)
			n = n*8 + (*cp-'0');
		umask(n);
	}
	return(0);
}

doexec(t)
register struct op *t;
{
	register i;
	jmp_buf ex;
	xint *ofail;

	t->ioact = NULL;
	for(i = 0; (t->words[i]=t->words[i+1]) != NULL; i++)
		;
	if (i == 0)
		return(1);
	execflg = 1;
	ofail = failpt;
	if (setjmp(failpt = ex) == 0)
		execute(t, NOPIPE, NOPIPE, FEXEC);
	failpt = ofail;
	execflg = 0;
	return(1);
}

dodot(t)
struct op *t;
{
	register i;
	register char *sp, *tp;
	char *cp;

	if ((cp = t->words[1]) == NULL)
		return(0);
	sp = any('/', cp)? ":": path->value;
	while (*sp) {
		tp = e.linep;
		while (*sp && (*tp = *sp++) != ':')
			tp++;
		if (tp != e.linep)
			*tp++ = '/';
		for (i = 0; (*tp++ = cp[i++]) != '\0';)
			;
		if ((i = open(e.linep, 0)) >= 0) {
			exstat = 0;
			next(remap(i));
			return(exstat);
		}
	}
	prs(cp);
	err(": not found");
	return(-1);
}

dowait(t)
struct op *t;
{
	register i;
	register char *cp;

	if ((cp = t->words[1]) != NULL) {
		i = getn(cp);
		if (i == 0)
			return(0);
	} else
		i = -1;
	if (talking)
		signal(SIGINT, onintr);
	setstatus(waitfor(i, 1));
	if (talking)
		signal(SIGINT, SIG_IGN);
	return(0);
}

doread(t)
struct op *t;
{
	register char *cp, **wp;
	register nb;

	if (t->words[1] == NULL) {
		err("Usage: read name ...");
		return(1);
	}
	for (wp = t->words+1; *wp; wp++) {
		for (cp = e.linep; cp < elinep-1; cp++)
			if ((nb = read(0, cp, sizeof(*cp))) != sizeof(*cp) ||
			    *cp == '\n' ||
			    wp[1] && any(*cp, ifs->value))
				break;
		*cp = 0;
		if (nb <= 0)
			break;
		setval(lookup(*wp), e.linep);
	}
	return(nb <= 0);
}

doeval(t)
register struct op *t;
{
	int wdchar();

	return(RUN(awordlist, t->words+1, wdchar));
}

dotrap(t)
register struct op *t;
{
	register char *s;
	register n, i;

	if (t->words[1] == NULL) {
		for (i=0; i<NSIG; i++)
			if (trap[i]) {
				prn(i);
				prs(": ");
				prs(trap[i]);
				prs("\n");
			}
		return(0);
	}
	n = getsig((s = t->words[2])!=NULL? s: t->words[1]);
	xfree(trap[n]);
	trap[n] = 0;
	if (s != NULL) {
		if ((i = strlen(s = t->words[1])) != 0) {
			trap[n] = strsave(s, 0);
			setsig(n, sig);
		} else
			setsig(n, SIG_IGN);
	} else
		setsig(n, (n == SIGINT || n == SIGQUIT) && talking? SIG_IGN: SIG_DFL);
	return(0);
}

getsig(s)
char *s;
{
	register int n;

	if ((n = getn(s)) < 0 || n >= NSIG) {
		err("trap: bad signal number");
		n = 0;
	}
	return(n);
}

setsig(n, f)
register n;
int (*f)();
{
	if (n == 0)
		return;
	if (signal(n, SIG_IGN) != SIG_IGN || ourtrap[n]) {
		ourtrap[n] = 1;
		signal(n, f);
	}
}

getn(as)
char *as;
{
	register char *s;
	register n, m;

	s = as;
	m = 1;
	if (*s == '-') {
		m = -1;
		s++;
	}
	for (n = 0; digit(*s); s++)
		n = (n*10) + (*s-'0');
	if (*s) {
		prs(as);
		err(": bad number");
	}
	return(n*m);
}

dobreak(t)
struct op *t;
{
	return(brkcontin(t->words[1], 1));
}

docontinue(t)
struct op *t;
{
	return(brkcontin(t->words[1], 0));
}

static int
brkcontin(cp, val)
register char *cp;
{
	register struct brkcon *bc;
	register nl;

	nl = cp == NULL? 1: getn(cp);
	if (nl <= 0)
		nl = 999;
	do {
		if ((bc = brklist) == NULL)
			break;
		brklist = bc->nextlev;
	} while (--nl);
	if (nl) {
		err("bad break/continue level");
		return(1);
	}
	isbreak = val;
	longjmp(bc->brkpt, 1);
	/* NOTREACHED */
}

doexit(t)
struct op *t;
{
	register char *cp;

	execflg = 0;
	if ((cp = t->words[1]) != NULL)
		exstat = getn(cp);
	leave();
}

doexport(t)
struct op *t;
{
	rdexp(t->words+1, export, EXPORT);
	return(0);
}

doreadonly(t)
struct op *t;
{
	rdexp(t->words+1, ronly, RONLY);
	return(0);
}

static void
rdexp(wp, f, key)
register char **wp;
void (*f)();
int key;
{
	if (*wp != NULL) {
		for (; *wp != NULL; wp++)
			if (checkname(*wp))
				(*f)(lookup(*wp));
			else
				badid(*wp);
	} else
		putvlist(key, 1);
}

static void
badid(s)
register char *s;
{
	prs(s);
	err(": bad identifier");
}

doset(t)
register struct op *t;
{
	register struct var *vp;
	register char *cp;
	register n;

	if ((cp = t->words[1]) == NULL) {
		for (vp = vlist; vp; vp = vp->next)
			varput(vp->name, 1);
		return(0);
	}
	if (*cp == '-') {
		t->words++;
		if (*++cp == 0)
			flag['x'] = flag['v'] = 0;
		else
			for (; *cp; cp++)
				switch (*cp) {
				case 'e':
					if (!talking)
						flag['e']++;
					break;

				default:
					if (*cp>='a' && *cp<='z')
						flag[*cp]++;
					break;
				}
		setdash();
	}
	if (t->words[1]) {
		t->words[0] = dolv[0];
		for (n=1; t->words[n]; n++)
			setarea((char *)t->words[n], 0);
		dolc = n-1;
		dolv = t->words;
		setval(lookup("#"), putn(dolc));
		setarea((char *)(dolv-1), 0);
	}
	return(0);
}

varput(s, out)
register char *s;
{
	if (letnum(*s)) {
		write(out, s, strlen(s));
		write(out, "\n", 1);
	}
}


struct	builtin {
	char	*command;
	int	(*fn)();
};
static struct	builtin	builtin[] = {
	":",		dolabel,
	"cd",		dochdir,
	"shift",	doshift,
	"exec",		doexec,
	"wait",		dowait,
	"read",		doread,
	"eval",		doeval,
	"trap",		dotrap,
	"break",	dobreak,
	"continue",	docontinue,
	"exit",		doexit,
	"export",	doexport,
	"readonly",	doreadonly,
	"set",		doset,
	".",		dodot,
	"umask",	doumask,
	"login",	dologin,
	"newgrp",	dologin,
	0,
};

int (*inbuilt(s))()
register char *s;
{
	register struct builtin *bp;

	for (bp = builtin; bp->command != NULL; bp++)
		if (strcmp(bp->command, s) == 0)
			return(bp->fn);
	return(NULL);
}

/* -------- eval.c -------- */
/* #include "sh.h" */
/* #include "word.h" */

/*
 * ${}
 * `command`
 * blank interpretation
 * quoting
 * glob
 */

static	char	*blank();
static	int	grave();
static	int	expand();
static	int	dollar();

char **
eval(ap, f)
register char **ap;
{
	struct wdblock *wb;
	char **wp;
	jmp_buf ev;

	inword++;
	wp = NULL;
	wb = NULL;
	if (newenv(setjmp(errpt = ev)) == 0) {
		wb = addword((char *)0, wb); /* space for shell name, if command file */
		while (expand(*ap++, &wb, f))
			;
		wb = addword((char *)0, wb);
		wp = getwords(wb) + 1;
		quitenv();
	} else
		gflg = 1;
	inword--;
	return(gflg? NULL: wp);
}

/*
 * Make the exported environment from the exported
 * names in the dictionary.  Keyword assignments
 * ought to be taken from wp (the list of words on the command line)
 * but aren't, yet. Until then: ARGSUSED
 */
char **
makenv(wp)
char **wp;
{
	register struct wdblock *wb;
	register struct var *vp;

	wb = NULL;
	for (vp = vlist; vp; vp = vp->next)
		if (vp->status & EXPORT)
			wb = addword(vp->name, wb);
	wb = addword((char *)0, wb);
	return(getwords(wb));
}

char *
evalstr(cp, f)
register char *cp;
int f;
{
	struct wdblock *wb;

	inword++;
	wb = NULL;
	if (expand(cp, &wb, f)) {
		if (wb == NULL || wb->w_nword == 0 || (cp = wb->w_words[0]) == NULL)
			cp = "";
		DELETE(wb);
	} else
		cp = NULL;
	inword--;
	return(cp);
}

static int
expand(cp, wbp, f)
register char *cp;
register struct wdblock **wbp;
{
	jmp_buf ev;

	gflg = 0;
	if (cp == NULL)
		return(0);
	if (!anys("$`'\"", cp) &&
	    !anys(ifs->value, cp) &&
	    ((f&DOGLOB)==0 || !anys("[*?", cp))) {
		cp = strsave(cp, areanum);
		if (f & DOTRIM)
			unquote(cp);
		*wbp = addword(cp, *wbp);
		return(1);
	}
	if (newenv(setjmp(errpt = ev)) == 0) {
		PUSHIO(aword, cp, strchar);
		e.iobase = e.iop;
		while ((cp = blank(f)) && gflg == 0) {
			e.linep = cp;
			cp = strsave(cp, areanum);
			if ((f&DOGLOB) == 0) {
				if (f & DOTRIM)
					unquote(cp);
				*wbp = addword(cp, *wbp);
			} else
				*wbp = glob(cp, *wbp);
		}
		quitenv();
	} else
		gflg = 1;
	return(gflg == 0);
}

/*
 * Blank interpretation and quoting
 */
static char *
blank(f)
{
	register c, c1;
	register char *sp;

	sp = e.linep;

loop:
	switch (c = subgetc('"', 0)) {
	case 0:
		if (sp == e.linep)
			return(0);
		*e.linep++ = 0;
		return(sp);

	default:
		if (f & DOBLANK && any(c, ifs->value))
			goto loop;
		break;

	case '"':
	case '\'':
		if (INSUB())
			break;
		for (c1 = c; (c = subgetc(c1, 1)) != c1;) {
			if (c == 0)
				break;
			if (c == '\'' || !any(c, "$`\""))
				c |= QUOTE;
			*e.linep++ = c;
		}
		c = 0;
	}
	unget(c);
	for (;;) {
		c = subgetc('"', 0);
		if (c == 0 ||
		    f & DOBLANK && any(c, ifs->value) ||
		    !INSUB() && any(c, "\"'`")) {
			unget(c);
			if (any(c, "\"'`"))
				goto loop;
			break;
		}
		*e.linep++ = c;
	}
	*e.linep++ = 0;
	return(sp);
}

/*
 * Get characters, substituting for ` and $
 */
int
subgetc(ec, quoted)
register char ec;
int quoted;
{
	register char c;

again:
	c = getc(ec);
	if (!INSUB() && ec != '\'') {
		if (c == '`') {
			if (grave(quoted) == 0)
				return(0);
			e.iop->task = XGRAVE;
			goto again;
		}
		if (c == '$' && (c = dollar(quoted)) == 0) {
			e.iop->task = XDOLL;
			goto again;
		}
	}
	return(c);
}

/*
 * Prepare to generate the string returned by ${} substitution.
 */
static int
dollar(quoted)
int quoted;
{
	int otask;
	struct io *oiop;
	char *dolp;
	register char *s, c, *cp;
	struct var *vp;

	c = readc();
	s = e.linep;
	if (c != '{') {
		*e.linep++ = c;
		if (letter(c)) {
			while ((c = readc())!=0 && letnum(c))
				if (e.linep < elinep)
					*e.linep++ = c;
			unget(c);
		}
		c = 0;
	} else {
		oiop = e.iop;
		otask = e.iop->task;
		e.iop->task = XOTHER;
		while ((c = subgetc('"', 0))!=0 && c!='}' && c!='\n')
			if (e.linep < elinep)
				*e.linep++ = c;
		if (oiop == e.iop)
			e.iop->task = otask;
		if (c != '}') {
			err("unclosed ${");
			gflg++;
			return(c);
		}
	}
	if (e.linep >= elinep) {
		err("string in ${} too long");
		gflg++;
		e.linep -= 10;
	}
	*e.linep = 0;
	if (*s)
		for (cp = s+1; *cp; cp++)
			if (any(*cp, "=-+?")) {
				c = *cp;
				*cp++ = 0;
				break;
			}
	if (s[1] == 0 && (*s == '*' || *s == '@')) {
		if (dolc > 1) {
			/* currently this does not distinguish $* and $@ */
			/* should check dollar */
			e.linep = s;
			PUSHIO(awordlist, dolv+1, dolchar);
			return(0);
		} else {	/* trap the nasty ${=} */
			s[0] = '1';
			s[1] = 0;
		}
	}
	vp = lookup(s);
	if ((dolp = vp->value) == null) {
		switch (c) {
		case '=':
			if (digit(*s)) {
				err("cannot use ${...=...} with $n");
				gflg++;
				break;
			}
			setval(vp, cp);
			dolp = vp->value;
			break;

		case '-':
			dolp = strsave(cp, areanum);
			break;

		case '?':
			if (*cp == 0) {
				prs("missing value for ");
				err(s);
			} else
				err(cp);
			gflg++;
			break;
		}
	} else if (c == '+')
		dolp = strsave(cp, areanum);
	if (flag['u'] && dolp == null) {
		prs("unset variable: ");
		err(s);
		gflg++;
	}
	e.linep = s;
	PUSHIO(aword, dolp, strchar);
	return(0);
}

/*
 * Run the command in `...` and read its output.
 */
static int
grave(quoted)
int quoted;
{
	register char *cp;
	register int i;
	int pf[2];

	for (cp = e.iop->arg.aword; *cp != '`'; cp++)
		if (*cp == 0) {
			err("no closing `");
			return(0);
		}
	if (openpipe(pf) < 0)
		return(0);
	if ((i = fork()) == -1) {
		closepipe(pf);
		err("try again");
		return(0);
	}
	if (i != 0) {
		e.iop->arg.aword = ++cp;
		close(pf[1]);
		PUSHIO(afile, remap(pf[0]), quoted? qgravechar: gravechar);
		return(1);
	}
	*cp = 0;
	/* allow trapped signals */
	for (i=0; i<NSIG; i++)
		if (ourtrap[i] && signal(i, SIG_IGN) != SIG_IGN)
			signal(i, SIG_DFL);
	dup2(pf[1], 1);
	closepipe(pf);
	flag['e'] = 0;
	flag['v'] = 0;
	flag['n'] = 0;
	cp = strsave(e.iop->arg.aword, 0);
	freearea(areanum = 1);	/* free old space */
	e.oenv = NULL;
	e.iop = (e.iobase = iostack) - 1;
	unquote(cp);
	talking = 0;
	PUSHIO(aword, cp, nlchar);
	onecommand();
	exit(1);
}

char *
unquote(as)
register char *as;
{
	register char *s;

	if ((s = as) != NULL)
		while (*s)
			*s++ &= ~QUOTE;
	return(as);
}

/* -------- glob.c -------- */
/* #include "sh.h" */

#define	DIRSIZ	14
struct	direct
{
	unsigned short	d_ino;
	char	d_name[DIRSIZ];
};
/*
 * glob
 */

#define	scopy(x) strsave((x), areanum)
#define	BLKSIZ	512
#define	NDENT	((BLKSIZ+sizeof(struct direct)-1)/sizeof(struct direct))

static	struct wdblock	*cl, *nl;
static	char	spcl[] = "[?*";
static	int	xstrcmp();
static	char	*generate();
static	int	anyspcl();

struct wdblock *
glob(cp, wb)
char *cp;
struct wdblock *wb;
{
	register i;
	register char *pp;

	if (cp == 0)
		return(wb);
	i = 0;
	for (pp = cp; *pp; pp++)
		if (any(*pp, spcl))
			i++;
		else if (!any(*pp & ~QUOTE, spcl))
			*pp &= ~QUOTE;
	if (i != 0) {
		for (cl = addword(scopy(cp), (struct wdblock *)0); anyspcl(cl); cl = nl) {
			nl = newword(cl->w_nword*2);
			for(i=0; i<cl->w_nword; i++) { /* for each argument */
				for (pp = cl->w_words[i]; *pp; pp++)
					if (any(*pp, spcl)) {
						globname(cl->w_words[i], pp);
						break;
					}
				if (*pp == '\0')
					nl = addword(scopy(cl->w_words[i]), nl);
			}
			for(i=0; i<cl->w_nword; i++)
				DELETE(cl->w_words[i]);
			DELETE(cl);
		}
		for(i=0; i<cl->w_nword; i++)
			unquote(cl->w_words[i]);
		glob0((char *)cl->w_words, cl->w_nword, sizeof(char *), xstrcmp);
		if (cl->w_nword) {
			for (i=0; i<cl->w_nword; i++)
				wb = addword(cl->w_words[i], wb);
			DELETE(cl);
			return(wb);
		}
	}
	wb = addword(unquote(cp), wb);
	return(wb);
}

globname(we, pp)
char *we;
register char *pp;
{
	register char *np, *cp;
	char *name, *gp, *dp;
	int dn, j, n, k;
	struct direct ent[NDENT];
	char dname[DIRSIZ+1];
	struct stat dbuf;

	for (np = we; np != pp; pp--)
		if (pp[-1] == '/')
			break;
	for (dp = cp = space(pp-np+3); np < pp;)
		*cp++ = *np++;
	*cp++ = '.';
	*cp = '\0';
	for (gp = cp = space(strlen(pp)+1); *np && *np != '/';)
		*cp++ = *np++;
	*cp = '\0';
	dn = open(dp, 0);
	if (dn < 0) {
		DELETE(dp);
		DELETE(gp);
		return;
	}
	dname[DIRSIZ] = '\0';
	while ((n = read(dn, (char *)ent, sizeof(ent))) >= sizeof(*ent)) {
		n /= sizeof(*ent);
		for (j=0; j<n; j++) {
			if (ent[j].d_ino == 0)
				continue;
			strncpy(dname, ent[j].d_name, DIRSIZ);
			if (dname[0] == '.' &&
			    (dname[1] == '\0' || dname[1] == '.' && dname[2] == '\0'))
				if (*gp != '.')
					continue;
			for(k=0; k<DIRSIZ; k++)
				if (any(dname[k], spcl))
					dname[k] |= QUOTE;
			if (gmatch(dname, gp)) {
				name = generate(we, pp, dname, np);
				if (*np && !anys(np, spcl)) {
					if (stat(name,&dbuf)) {
						DELETE(name);
						continue;
					}
				}
				nl = addword(name, nl);
			}
		}
	}
	close(dn);
	DELETE(dp);
	DELETE(gp);
}

/*
 * generate a pathname as below.
 * start..end1 / middle end
 * the slashes come for free
 */
static char *
generate(start1, end1, middle, end)
char *start1;
register char *end1;
char *middle, *end;
{
	char *p;
	register char *op, *xp;

	p = op = space(end1-start1+strlen(middle)+strlen(end)+2);
	for (xp = start1; xp != end1;)
		*op++ = *xp++;
	for (xp = middle; (*op++ = *xp++) != '\0';)
		;
	op--;
	for (xp = end; (*op++ = *xp++) != '\0';)
		;
	return(p);
}

static int
anyspcl(wb)
register struct wdblock *wb;
{
	register i;
	register char **wd;

	wd = wb->w_words;
	for (i=0; i<wb->w_nword; i++)
		if (anys(spcl, *wd++))
			return(1);
	return(0);
}

static int
xstrcmp(p1, p2)
char *p1, *p2;
{
	return(strcmp(*(char **)p1, *(char **)p2));
}

/* -------- word.c -------- */
/* #include "sh.h" */
/* #include "word.h" */
char *memcpy();

#define	NSTART	16	/* default number of words to allow for initially */

struct wdblock *
newword(nw)
register nw;
{
	register struct wdblock *wb;

	wb = (struct wdblock *) space(sizeof(*wb) + nw*sizeof(char *));
	wb->w_bsize = nw;
	wb->w_nword = 0;
	return(wb);
}

struct wdblock *
addword(wd, wb)
char *wd;
register struct wdblock *wb;
{
	register struct wdblock *wb2;
	register nw;

	if (wb == NULL)
		wb = newword(NSTART);
	if ((nw = wb->w_nword) >= wb->w_bsize) {
		wb2 = newword(nw * 2);
		memcpy((char *)wb2->w_words, (char *)wb->w_words, nw*sizeof(char *));
		wb2->w_nword = nw;
		DELETE(wb);
		wb = wb2;
	}
	wb->w_words[wb->w_nword++] = wd;
	return(wb);
}

char **
getwords(wb)
register struct wdblock *wb;
{
	register char **wd;
	register nb;

	if (wb == NULL)
		return(NULL);
	if (wb->w_nword == 0) {
		DELETE(wb);
		return(NULL);
	}
	wd = (char **) space(nb = sizeof(*wd) * wb->w_nword);
	memcpy((char *)wd, (char *)wb->w_words, nb);
	DELETE(wb);	/* perhaps should done by caller */
	return(wd);
}

int	(*func)();
int	globv;

glob0(a0, a1, a2, a3)
char *a0;
unsigned a1;
int a2;
int (*a3)();
{
	func = a3;
	globv = a2;
	glob1(a0, a0 + a1 * a2);
}

glob1(base, lim)
char *base, *lim;
{
	register char *i, *j;
	int v2;
	char **k;
	char *lptr, *hptr;
	int c;
	unsigned n;


	v2 = globv;

top:
	if ((n=lim-base) <= v2)
		return;
	n = v2 * (n / (2*v2));
	hptr = lptr = base+n;
	i = base;
	j = lim-v2;
	for(;;) {
		if (i < lptr) {
			if ((c = (*func)(i, lptr)) == 0) {
				glob2(i, lptr -= v2);
				continue;
			}
			if (c < 0) {
				i += v2;
				continue;
			}
		}

begin:
		if (j > hptr) {
			if ((c = (*func)(hptr, j)) == 0) {
				glob2(hptr += v2, j);
				goto begin;
			}
			if (c > 0) {
				if (i == lptr) {
					glob3(i, hptr += v2, j);
					i = lptr += v2;
					goto begin;
				}
				glob2(i, j);
				j -= v2;
				i += v2;
				continue;
			}
			j -= v2;
			goto begin;
		}


		if (i == lptr) {
			if (lptr-base >= lim-hptr) {
				glob1(hptr+v2, lim);
				lim = lptr;
			} else {
				glob1(base, lptr);
				base = hptr+v2;
			}
			goto top;
		}


		glob3(j, lptr -= v2, i);
		j = hptr -= v2;
	}
}

glob2(i, j)
char *i, *j;
{
	register char *index1, *index2, c;
	int m;

	m = globv;
	index1 = i;
	index2 = j;
	do {
		c = *index1;
		*index1++ = *index2;
		*index2++ = c;
	} while(--m);
}

glob3(i, j, k)
char *i, *j, *k;
{
	register char *index1, *index2, *index3;
	int c;
	int m;

	m = globv;
	index1 = i;
	index2 = j;
	index3 = k;
	do {
		c = *index1;
		*index1++ = *index3;
		*index3++ = *index2;
		*index2++ = c;
	} while(--m);
}

/* -------- io.c -------- */
/* #include "sh.h" */

/*
 * shell IO
 */


int
getc(ec)
register int ec;
{
	register int c;

	if(e.linep > elinep) {
		while((c=readc()) != '\n' && c)
			;
		err("input line too long");
		gflg++;
		return(c);
	}
	c = readc();
	if (ec != '\'') {
		if(c == '\\') {
			c = readc();
			if (c == '\n' && ec != '\"')
				return(getc(ec));
			c |= QUOTE;
		}
	}
	return(c);
}

void
unget(c)
{
	if (e.iop >= e.iobase)
		e.iop->peekc = c;
}

int
readc()
{
	register c;
	static int eofc;

	for (; e.iop >= e.iobase; e.iop--)
		if ((c = e.iop->peekc) != '\0') {
			e.iop->peekc = 0;
			return(c);
		} else if ((c = (*e.iop->iofn)(&e.iop->arg, e.iop)) != '\0') {
			if (c == -1) {
				e.iop++;
				continue;
			}
			if (e.iop == iostack)
				ioecho(c);
			return(c);
		}
	if (e.iop >= iostack ||
	    multiline && eofc++ < 3)
		return(0);
	leave();
	/* NOTREACHED */
}

void
ioecho(c)
char c;
{
	if (flag['v'])
		write(2, &c, sizeof c);
}

void
pushio(arg, fn)
struct ioarg arg;
int (*fn)();
{
	if (++e.iop >= &iostack[NPUSH]) {
		e.iop--;
		err("Shell input nested too deeply");
		gflg++;
		return;
	}
	e.iop->iofn = fn;
	e.iop->arg = arg;
	e.iop->peekc = 0;
	e.iop->xchar = 0;
	e.iop->nlcount = 0;
	if (fn == filechar || fn == linechar || fn == nextchar)
		e.iop->task = XIO;
	else if (fn == gravechar || fn == qgravechar)
		e.iop->task = XGRAVE;
	else
		e.iop->task = XOTHER;
}

struct io *
setbase(ip)
struct io *ip;
{
	register struct io *xp;

	xp = e.iobase;
	e.iobase = ip;
	return(xp);
}

/*
 * Input generating functions
 */

/*
 * Produce the characters of a string, then a newline, then EOF.
 */
int
nlchar(ap)
register struct ioarg *ap;
{
	register int c;

	if (ap->aword == NULL)
		return(0);
	if ((c = *ap->aword++) == 0) {
		ap->aword = NULL;
		return('\n');
	}
	return(c);
}

/*
 * Given a list of words, produce the characters
 * in them, with a space after each word.
 */
int
wdchar(ap)
register struct ioarg *ap;
{
	register char c;
	register char **wl;

	if ((wl = ap->awordlist) == NULL)
		return(0);
	if (*wl != NULL) {
		if ((c = *(*wl)++) != 0)
			return(c & 0177);
		ap->awordlist++;
		return(' ');
	}
	ap->awordlist = NULL;
	return('\n');
}

/*
 * Return the characters of a list of words,
 * producing a space between them.
 */
static	int	xxchar(), qqchar();

int
dolchar(ap)
register struct ioarg *ap;
{
	register char *wp;

	if ((wp = *ap->awordlist++) != NULL) {
		PUSHIO(aword, wp, *ap->awordlist == NULL? qqchar: xxchar);
		return(-1);
	}
	return(0);
}

static int
xxchar(ap)
register struct ioarg *ap;
{
	register int c;

	if (ap->aword == NULL)
		return(0);
	if ((c = *ap->aword++) == '\0') {
		ap->aword = NULL;
		return(' ');
	}
	return(c);
}

static int
qqchar(ap)
register struct ioarg *ap;
{
	register int c;

	if (ap->aword == NULL || (c = *ap->aword++) == '\0')
		return(0);
	return(c);
}

/*
 * Produce the characters from a single word (string).
 */
int
strchar(ap)
register struct ioarg *ap;
{
	register int c;

	if (ap->aword == 0 || (c = *ap->aword++) == 0)
		return(0);
	return(c);
}

/*
 * Return the characters from a file.
 */
int
filechar(ap)
register struct ioarg *ap;
{
	register int i;
	char c;
	extern int errno;

	do {
		i = read(ap->afile, &c, sizeof(c));
	} while (i < 0 && errno == EINTR);
	return(i == sizeof(c)? c&0177: (closef(ap->afile), 0));
}

/*
 * Return the characters produced by a process (`...`).
 * Quote them if required, and remove any trailing newline characters.
 */
int
gravechar(ap, iop)
struct ioarg *ap;
struct io *iop;
{
	register int c;

	if ((c = qgravechar(ap, iop)&~QUOTE) == '\n')
		c = ' ';
	return(c);
}

int
qgravechar(ap, iop)
register struct ioarg *ap;
struct io *iop;
{
	register int c;

	if (iop->xchar) {
		if (iop->nlcount) {
			iop->nlcount--;
			return('\n'|QUOTE);
		}
		c = iop->xchar;
		iop->xchar = 0;
	} else if ((c = filechar(ap)) == '\n') {
		iop->nlcount = 1;
		while ((c = filechar(ap)) == '\n')
			iop->nlcount++;
		iop->xchar = c;
		if (c == 0)
			return(c);
		iop->nlcount--;
		c = '\n';
	}
	return(c!=0? c|QUOTE: 0);
}

/*
 * Return a single command (usually the first line) from a file.
 */
int
linechar(ap)
register struct ioarg *ap;
{
	register int c;

	if ((c = filechar(ap)) == '\n') {
		if (!multiline) {
			closef(ap->afile);
			ap->afile = -1;	/* illegal value */
		}
	}
	return(c);
}

/*
 * Return the next character from the command source,
 * prompting when required.
 */
int
nextchar(ap)
register struct ioarg *ap;
{
	register int c;

	if ((c = filechar(ap)) != 0)
		return(c);
	if (talking && e.iop <= iostack+1)
		prs(prompt->value);
	return(0);
}

void
prs(s)
register char *s;
{
	if (*s)
		write(2, s, strlen(s));
}

void
putc(c)
char c;
{
	write(2, &c, sizeof c);
}

void
prn(u)
unsigned u;
{
	prs(itoa(u, 0));
}

void
closef(i)
register i;
{
	if (i > 2)
		close(i);
}

void
closeall()
{
	register u;

	for (u=NUFILE; u<NOFILE;)
		close(u++);
}

/*
 * remap fd into Shell's fd space
 */
int
remap(fd)
register int fd;
{
	register int i;
	int map[NOFILE];

	if (fd < e.iofd) {
		for (i=0; i<NOFILE; i++)
			map[i] = 0;
		do {
			map[fd] = 1;
			fd = dup(fd);
		} while (fd >= 0 && fd < e.iofd);
		for (i=0; i<NOFILE; i++)
			if (map[i])
				close(i);
		if (fd < 0)
			err("too many files open in shell");
	}
	return(fd);
}

int
openpipe(pv)
register int *pv;
{
	register int i;

	if ((i = pipe(pv)) < 0)
		err("can't create pipe - try again");
	return(i);
}

void
closepipe(pv)
register int *pv;
{
	if (pv != NULL) {
		close(*pv++);
		close(*pv);
	}
}

/* -------- here.c -------- */
/* #include "sh.h" */
char *memcpy();

/*
 * here documents
 */

struct	here {
	char	*h_tag;
	int	h_dosub;
	struct	ioword *h_iop;
	struct	here	*h_next;
} *herelist;

struct	block {
	char	*b_start;
	char	*b_next;
	char	*b_line;
	int	b_size;
};

static	struct block *readhere();

#define	NCPB	100	/* here text block allocation unit */

markhere(s, iop)
register char *s;
struct ioword *iop;
{
	register struct here *h, *lh;

	h = (struct here *) space(sizeof(struct here));
	if (h == 0)
		return;
	h->h_tag = evalstr(s, DOSUB);
	if (h->h_tag == 0)
		return;
	h->h_iop = iop;
	h->h_next = NULL;
	if (herelist == 0)
		herelist = h;
	else
		for (lh = herelist; lh!=NULL; lh = lh->h_next)
			if (lh->h_next == 0) {
				lh->h_next = h;
				break;
			}
	iop->io_flag |= IOHERE|IOXHERE;
	for (s = h->h_tag; *s; s++)
		if (*s & QUOTE) {
			iop->io_flag &= ~ IOXHERE;
			*s &= ~ QUOTE;
		}
	h->h_dosub = iop->io_flag & IOXHERE;
}

gethere()
{
	register struct here *h;

	for (h = herelist; h != NULL; h = h->h_next)
		h->h_iop->io_un.io_here = readhere(h->h_tag, h->h_dosub? 0: '\'');
	herelist = NULL;
}


static int savec(c, bp_int)
{
	struct block* bp = (struct block*) bp_int;
	register char *np;

	if (bp->b_start == NULL || bp->b_next+1 >= bp->b_start+bp->b_size) {
		np = space(bp->b_size + NCPB);
		if (np == 0)
			return(0);
		memcpy(np, bp->b_start, bp->b_size);
		bp->b_size += NCPB;
		bp->b_line = np + (bp->b_line-bp->b_start);
		bp->b_next = np + (bp->b_next-bp->b_start);
		xfree(bp->b_start);
		bp->b_start = np;
	}
	*bp->b_next++ = c;
	return(1);
}


static struct block *
readhere(s, ec)
register char *s;
{
	register struct block *bp;
	register c;
	jmp_buf ev;

	bp = (struct block *) space(sizeof(*bp));
	if (bp == 0)
		return(0);
	if (newenv(setjmp(errpt = ev)) == 0) {
		if (e.iop == iostack && e.iop->iofn == filechar) {
			pushio(e.iop->arg, filechar);
			e.iobase = e.iop;
		}
		bp->b_size = 0;
		bp->b_line = 0;
		bp->b_next = 0;
		bp->b_start = 0;
		for (;;) {
			while ((c = getc(ec)) != '\n' && c) {
				if (ec == '\'')
					c &= ~ QUOTE;
				if (savec(c, bp) == 0) {
					c = 0;
					break;
				}
			}
			savec(0, bp);
			if (strcmp(s, bp->b_line) == 0 || c == 0)
				break;
			bp->b_next[-1] = '\n';
			bp->b_line = bp->b_next;
		}
		*bp->b_line = 0;
		if (c == 0) {
			prs("here document `"); prs(s); err("' unclosed");
		}
		quitenv();
	}
	return(bp);
}

herein(bp, xdoll)
struct block *bp;
{
	register tf;
	char tname[50];
	static int inc;
	register char *cp, *lp;

	if (bp == 0)
		return(-1);
	for (cp = tname, lp = "/tmp/shtm"; (*cp = *lp++) != '\0'; cp++)
		;
	lp = putn(getpid()*100 + inc++);
	for (; (*cp = *lp++) != '\0'; cp++)
		;
	if ((tf = creat(tname, 0666)) >= 0) {
		if (xdoll) {
			char c;
			jmp_buf ev;

			if (newenv(setjmp(errpt = ev)) == 0) {
				PUSHIO(aword, bp->b_start, strchar);
				setbase(e.iop);
				while ((c = subgetc(0, 0)) != 0) {
					c &= ~ QUOTE;
					write(tf, &c, sizeof c);
				}
				quitenv();
			} else
				unlink(tname);
		} else
			write(tf, bp->b_start, bp->b_line-bp->b_start);
		close(tf);
		tf = open(tname, 0);
		unlink(tname);
	}
	return(tf);
}

scraphere()
{
	herelist = NULL;
}

char *
memcpy(ato, from, nb)
register char *ato, *from;
register int nb;
{
	register char *to;

	to = ato;
	while (--nb >= 0)
		*to++ = *from++;
	return(ato);
}
