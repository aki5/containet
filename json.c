/*
 *	Copyright (c) 2016 Aki Nyrhinen
 *
 *	Permission is hereby granted, free of charge, to any person obtaining a copy
 *	of this software and associated documentation files (the "Software"), to deal
 *	in the Software without restriction, including without limitation the rights
 *	to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *	copies of the Software, and to permit persons to whom the Software is
 *	furnished to do so, subject to the following conditions:
 *
 *	The above copyright notice and this permission notice shall be included in
 *	all copies or substantial portions of the Software.
 *
 *	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 *	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *	OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 *	THE SOFTWARE.
 */
#include <stdio.h>
#include <stdint.h>
#include "json.h"

#define likely(x) __builtin_expect((x),1)
#define unlikely(x) __builtin_expect((x),0)

static __thread char *jsonfile = "";
static __thread int jsonline;

static int
isterminal(int c)
{
	switch(c){
	case JsonArray:
	case JsonObject:
	case JsonNumber:
	case JsonString:
	case JsonSymbol:
		return 1;
	}
	return 0;
}

static int
isnumber(int c)
{
	switch(c){
	case '0': case '1': case '2': case '3': case '4':
	case '5': case '6': case '7': case '8': case '9':
/**/
	case 'e': case 'E':
	case '+': case '-':
	case '.':
		return 1;
	}
	return 0;
}

static int
iswhite(int c)
{
	switch(c){
	case '\t': case '\n': case '\v': case '\f': case '\r': case ' ':
		return 1;
	}
	return 0;
}

static int
isbreak(int c)
{
	switch(c){
	case '{': case '}':
	case '[': case ']':
	//case '(': case ')':
	case ':': case ',':
	case '+': case '-':
	case '.':
	case '"':

	case '\t': case '\n': case '\v': case '\f': case '\r': case ' ':
		return 1;
	}
	return 0;
}

// notice that all the switch-case business and string scanning looks for ascii values < 128,
// hence there is no need to decode utf8 explicitly, it just works.
static int
jsonlex(char *buf, int *offp, int *lenp, char **tokp)
{
	char *str;
	int len, qch;

	str = buf + *offp;
	len = *lenp;

again:
	while(len > 0 && iswhite(*str)){
		if(*str == '\n')
			jsonline++;
		str++;
		len--;
	}
	*tokp = str;
	if(len > 0){
		switch(*str){

		// c and c++ style comments, not standard either
		case '/':
			if(len > 1 && str[1] == '/'){
				while(len > 1 && str[1] != '\n'){
					str++;
					len--;
				}
				// advance to newline
				if(len > 0){
					str++;
					len--;
				}
				// skip newline
				if(len > 0 && *str == '\n'){
					jsonline++;
					str++;
					len--;
				}
				goto again;
			} else if(len > 1 && str[1] == '*'){
				while(len > 1 && !(str[0] == '*' && str[1] == '/')){
					if(str[1] == '\n')
						jsonline++;
					str++;
					len--;
				}
				// skip '*'
				if(len > 0){
					str++;
					len--;
				}
				// skip '/'
				if(len > 0){
					str++;
					len--;
				}
				goto again;
			}
			goto caseself;

		// straight forward return self.
		case '{': case '}':
		case '[': case ']':
		//case '(': case ')':
		case ':':
		case ',':
		caseself:
			*offp = str+1-buf;
			*lenp = len-1;
			return *str;

		// it may be number, look at the next character and decide.
		case '+': case '-': case '.':
			if(len < 2)
				goto caseself;
			if(str[1] < '0' || str[1] > '9')
				goto caseself;
			str++;
			len--;
			/* fall through */

		// the number matching algorithm is a bit fast and loose, but it does the business.
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			while(len > 1 && isnumber(str[1])){
				str++;
				len--;
			}
			*offp = str+1-buf;
			*lenp = len-1;
			return JsonNumber;

		// nonstandard single quote strings..
		case '\'':
			qch = '\'';
			goto casestr;

		// string constant, detect and skip escapes but don't interpret them
		case '"':
			qch = '"';
		casestr:
			while(len > 1 && str[1] != qch){
				if(unlikely(len > 1 && str[1] == '\\')){
					switch(str[2]){
					default:
						str++;
						len--;
						break;
					case 'u':
						str += len < 5 ? len : 5;
						len -= len < 5 ? len : 5;
						break;
					}
				}
				str++;
				len--;
			}
			if(len > 1 && str[1] == qch){
				str++;
				len--;
			}
			*offp = str+1-buf;
			*lenp = len-1;
			return JsonString;

		// symbol is any string of nonbreak characters not starting with a number
		default:
			while(len > 1 && !isbreak(str[1])){
				str++;
				len--;
			}
			*offp = str+1-buf;
			*lenp = len-1;
			return JsonSymbol;
		}
	}
	// set them here too, so that we don't re-read the last character indefinitely
	*offp = str+1-buf;
	*lenp = len-1;
	return -1;
}

int
jsonarray(JsonAst *ast, int *astoff, int jscap, char *buf, int *offp, int *lenp)
{
	int patch, lt;
	int ret;

	// array begin
	patch = *astoff;
	if(patch < jscap){
		ast[patch].type = '[';
		ast[patch].off = -1;
		ast[patch].len = -1;
	}
	*astoff = patch+1;

	ret = '[';
	for(;;){
		lt = jsonparse(ast, astoff, jscap, buf, offp, lenp);
nocomma:
		if(lt == -1){
			ret = -1;
			break;
		}
		if(lt == ']')
			break;
		if(!isterminal(lt))
			fprintf(stderr, "%s:%d: jsonarray: got '%c', expecting object or terminal\n", jsonfile, jsonline, lt);

		lt = jsonparse(ast, astoff, jscap, buf, offp, lenp);
		if(lt == -1){
			ret = -1;
			break;
		}
		if(lt == ']')
			break;
		if(lt != ','){
			fprintf(stderr, "%s:%d: jsonarray: got '%c', expecting ','\n", jsonfile, jsonline, lt);
			goto nocomma;
		}
	}
	if(patch < jscap)
		ast[patch].next = *astoff + 1;

	// array end
	patch = *astoff;
	if(patch < jscap){
		ast[patch].type = ']';
		ast[patch].off = -1;
		ast[patch].len = -1;
		ast[patch].next = patch+1;
	}
	*astoff = patch+1;
	return ret;
}


int
jsonobject(JsonAst *ast, int *astoff, int jscap, char *buf, int *offp, int *lenp)
{
	int patch, lt;
	int ret;

	// object begin
	patch = *astoff;
	if(patch < jscap){
		ast[patch].type = '{';
		ast[patch].off = -1;
		ast[patch].len = -1;
	}
	*astoff = patch+1;

	ret = '{';
	for(;;){
		lt = jsonparse(ast, astoff, jscap, buf, offp, lenp);
nocomma:
		if(lt == -1){
			ret = -1;
			break;
		}
		if(lt == '}')
			break;
		if(lt != JsonString)
			fprintf(stderr, "%s:%d: jsonobject: got '%c', expecting key ('\"')\n", jsonfile, jsonline, lt);

		lt = jsonparse(ast, astoff, jscap, buf, offp, lenp);
		if(lt == -1){
			ret = -1;
			break;
		}
		if(lt != ':')
			fprintf(stderr, "%s:%d: jsonobject: got '%c', expecting colon (':')\n", jsonfile, jsonline, lt);

		lt = jsonparse(ast, astoff, jscap, buf, offp, lenp);
		if(lt == -1){
			ret = -1;
			break;
		}
		if(!isterminal(lt))
			fprintf(stderr, "%s:%d: jsonobject: got '%c', expecting object or terminal\n", jsonfile, jsonline, lt);

		lt = jsonparse(ast, astoff, jscap, buf, offp, lenp);
		if(lt == -1){
			ret = -1;
			break;
		}
		if(lt == '}')
			break;
		if(lt != ','){
			fprintf(stderr, "%s:%d: jsonobject: got '%c', expecting ','\n", jsonfile, jsonline, lt);
			goto nocomma;
		}

	}
	if(patch < jscap)
		ast[patch].next = *astoff + 1;

	// object end
	patch = *astoff;
	if(patch < jscap){
		ast[patch].type = '}';
		ast[patch].off = -1;
		ast[patch].len = -1;
		ast[patch].next = patch+1;
	}
	*astoff = patch+1;
	return ret;
}

void
jsonsetname(char *filename)
{
	jsonline = 1;
	jsonfile = filename;
}

int
jsonparse(JsonAst *ast, int *astoff, int jscap, char *buf, int *offp, int *lenp)
{
	char *tok;
	int patch, lt;

	switch(lt = jsonlex(buf, offp, lenp, &tok)){
	case -1:
		return -1;
	case '[':
		return jsonarray(ast, astoff, jscap, buf, offp, lenp);
	case '{':
		return jsonobject(ast, astoff, jscap, buf, offp, lenp);
	case ']': case '}': case ':': case ',':
		return lt;
	case JsonNumber:
	case JsonString:
	case JsonSymbol:
		patch = *astoff;
		if(patch < jscap){
			ast[patch].type = lt;
			ast[patch].off = tok-buf;
			ast[patch].len = *offp - (tok-buf);
			ast[patch].next = patch+1;
		}
		*astoff = patch+1;
		return lt;
	default:
		fprintf(stderr, "%s:%d: jsonparse: unexpected token: '", jsonfile, jsonline);
		fwrite(tok, *offp - (tok-buf), 1, stderr);
		fprintf(stderr, "'\n");
		return -1;
	}
}
