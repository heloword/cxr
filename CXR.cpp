/*********************************************************************

   Copyright (C) 2002 Smaller Animals Software, Inc.

   This software is provided 'as-is', without any express or implied
   warranty.  In no event will the authors be held liable for any damages
   arising from the use of this software.

   Permission is granted to anyone to use this software for any purpose,
   including commercial applications, and to alter it and redistribute it
   freely, subject to the following restrictions:

   1. The origin of this software must not be misrepresented; you must not
     claim that you wrote the original software. If you use this software
     in a product, an acknowledgment in the product documentation would be
     appreciated but is not required.

   2. Altered source versions must be plainly marked as such, and must not be
     misrepresented as being the original software.

   3. This notice may not be removed or altered from any source distribution.

   http://www.smalleranimals.com
   smallest@smalleranimals.com

**********************************************************************/

// CXR.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include "CXR.h"
#include "CmdLine.h"
#include "Tokenizer.h"
#include "Stream.h"


#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

/////////////////////////////////////////////////////////////////////////////
// The one and only application object

CWinApp theApp;

using namespace std;

/////////////////////////////////////////////////////////////////

static bool     ProcessFile(CStdioFile &in, CStdioFile &out);
static bool     AddDecode(const CString & csPassword, CStdioFile &out);
static CString  Encrypt(const CString &csIn, const char *pPass);
static CString  Decrypt(const char *pIn, const char *pPass);
static bool     ExpandOctal(const CString &csIn, CString &csOut, int &iConsumed);
static CString  TranslateCString(const CString &csIn);
static bool     ExpandHex(const CString &csIn, CString &csOut, int &iConsumed);
static CString    EscapeCString(const char *pIn);

// these can be adjusted in the range 1 to 239
const int basechar1 = 128;
const int basechar2 = 128;

/////////////////////////////////////////////////////////////////

int _tmain(int argc, char* argv[], char* envp[])
{
	int nRetCode = 0;

   srand(time(NULL));

	// initialize MFC and print and error on failure
	if (!AfxWinInit(::GetModuleHandle(NULL), NULL, ::GetCommandLine(), 0))
	{
		// TODO: change error code to suit your needs
		cerr << _T("Fatal Error: MFC initialization failed") << endl;
		nRetCode = 1;
	}
	else
	{
      cerr << "Starting CXR, the literal string encryptor. Copyright 2002, Smaller Animals Software Inc.\n";

      if ((basechar1 == 0) || (basechar2 == 0) || (basechar1 > 239) || (basechar2 > 239))
      {
         cerr << "CXR basechar values out of acceptable range. Aborting\n";
         nRetCode = 1;
         return nRetCode;
      }

      CCmdLine cmd;
      if (cmd.SplitLine(argc, argv) >= 2)
      {
         CString csInFile = cmd.GetSafeArgument("-i", 0, "");
         CString csOutFile = cmd.GetSafeArgument("-o", 0, "");
         if (!csInFile.IsEmpty() && !csOutFile.IsEmpty())
         {
		      // open the input file
            CStdioFile fileIn;

            // open the output file
            CStdioFile fileOut;
            

            if (fileIn.Open(csInFile, CFile::modeRead | CFile::typeText))
            {
               if (fileOut.Open(csOutFile, CFile::modeCreate | CFile::modeWrite | CFile::typeText ))
               {
                  if (!ProcessFile(fileIn, fileOut))
                  {
                     cerr << "CXR failed\n";
                     nRetCode = 1;
                  }
               }
               else
               {
                  cerr << _T("Unable to open output file: ") << (LPCTSTR)csOutFile << endl;
                  nRetCode = 1;
               }
            }
            else
            {
               cerr << _T("Unable to open input file: ") << (LPCTSTR)csInFile << endl;
               nRetCode = 1;
            }

            if (nRetCode==0)
            {
               cerr << "CXR created: " << (LPCTSTR)csOutFile << "\n";
            }
         }
         else
         {
            cerr << _T("Not enough parameters") << endl;
            nRetCode = 1;
         }
      }      
      else
      {
         cerr << _T("Not enough parameters") << endl;
         nRetCode = 1;
      }
	}

 	return nRetCode;
}

/////////////////////////////////////////////////////////////////

bool ProcessFile(CStdioFile &in, CStdioFile &out)
{
   enum 
   {
      eStateWantPassword,
      eStateHavePassword,
   };

   int iState = eStateWantPassword;

   CString csPassword;
   CString line;

   char *pMetachars = _T("/\\=();'");
   char *pKeyWords[3] = {_T("//"), _T("_CXR"), _T("CXRP")};
   
   CTokenizer tokenizer(pKeyWords, 3, pMetachars, strlen(pMetachars));
   int iErr = CTokenizer::eErrorNone;
   bool ok = true;

   out.WriteString(_T("/////////////////////////////////////////////////////////////\n"));
   out.WriteString(_T("// "));
   out.WriteString(out.GetFileName());
   out.WriteString(_T("\n//\n"));
   out.WriteString(_T("// This file was generated by CXR, the literal string encryptor.\n"));
   out.WriteString(_T("// CXR, Copyright 2002, Smaller Animals Software, Inc., all rights reserved.\n"));
   out.WriteString(_T("//\n"));
   out.WriteString(_T("// Please do not edit this file. Any changes here will be overwritten on the next compile.\n// If you wish to make changes to a string, please edit:\n//     "));
   out.WriteString(in.GetFilePath());
   out.WriteString(_T("\n//\n"));
   out.WriteString(_T("\n/////////////////////////////////////////////////////////////\n\n"));
   out.WriteString(_T("#include \"stdafx.h\"\n"));
   out.WriteString(_T("#include \"cxr_inc.h\"\n\n"));

   bool bFoundCXR = false;

   do 
   {
      if (!in.ReadString(line))
      {
         break;
      }

      switch (iState)
      {
      case eStateWantPassword:
         iErr = tokenizer.Tokenize(line);
         if (iErr == CTokenizer::eErrorNone)
         {
            if (tokenizer.GetTokenCount() >= 4)
            {
               // password declaration always looks like : // CXRP = "Password"
               if ((tokenizer.GetToken(0).csToken == _T("//")) && 
                  (tokenizer.GetToken(1).csToken == _T("CXRP")) && 
                  (tokenizer.GetToken(2).csToken == _T("=")) && 
                  (tokenizer.GetToken(3).bIsQuotedString))
               {
                  // we'll use the password from the file, literally. it's not treated as
                  // a C string-literal, just as a section of a text file. when we
                  // go to write the decoder, we'll have to fix it up to make sure
                  // the compiler gets the same text by adding any necessary escapes.
                  csPassword = tokenizer.GetToken(3).csToken;

                  if (csPassword.IsEmpty())
                  {
                     cerr << _T("Invalid CXR password: \"") << (LPCTSTR)csPassword << _T("\"") << endl;
                     ASSERT(0);
                     break;
                  }

                  iState = eStateHavePassword;
                  continue;
               }
            }
         }
         break;
      case eStateHavePassword:
         bFoundCXR = false;
         iErr = tokenizer.Tokenize(line);
         if (iErr == CTokenizer::eErrorNone)
         {
            if (tokenizer.GetTokenCount() > 4)
            {
               for (int i=0;i<tokenizer.GetTokenCount() - 4; i++)
               {
                  // looking for _CXR ( "..." )
                  if (
                     (tokenizer.GetToken(i).csToken == _T("_CXR")) && !tokenizer.GetToken(i).bIsQuotedString &&
                     (tokenizer.GetToken(i + 1).csToken == _T("(")) && !tokenizer.GetToken(i + 1).bIsQuotedString &&
                     (tokenizer.GetToken(i + 2).bIsQuotedString) &&
                     (tokenizer.GetToken(i + 3).csToken == _T(")")) && !tokenizer.GetToken(i + 3).bIsQuotedString
                     )
                  {
                     CString csTrans = TranslateCString(tokenizer.GetToken(i + 2).csToken);
                     CString csEnc = Encrypt(csTrans, csPassword);
                     //CString csDec = Decrypt(csEnc, csPassword);

                     out.WriteString(_T("///////////////////////////\n#ifdef _USING_CXR\n"));

                     /*
                     out.WriteString("//");
                     out.WriteString(csDec);
                     out.WriteString("\n");
                     */

                     // output up to _CXR
                     out.WriteString(line.Left(tokenizer.GetToken(i).iStart));

                     // encrypted stuff
                     out.WriteString(_T("\""));
                     out.WriteString(csEnc);
                     out.WriteString(_T("\""));

                     // to the end of the line
                     out.WriteString(line.Mid(tokenizer.GetToken(i + 4).iStop));

                     out.WriteString(_T("\n"));
 
                     out.WriteString(_T("#else\n"));
                     out.WriteString(line);
                     out.WriteString(_T("\n#endif\n\n"));

                     bFoundCXR = true;

                     break;

                  } // found a good string ?
                  
               } // loop over tokens

            }  // > 4 tokens

         } // tokenizer OK

         if (bFoundCXR)
         {
            continue;
         }
      
         break; // switch
      }

      // done with it
      out.WriteString(line);
      out.WriteString("\n");

   } while (1);

   if (iState == eStateWantPassword)
   {
      cerr << "No password line found in input file\n";
      return false;
   }

   ASSERT(iState==eStateHavePassword);

   // add the decoder functions
   AddDecode(csPassword, out);

   return true;
}

/////////////////////////////////////////////////////////////////

void AddEncByte(BYTE c, CString &csOut)
{
  
   char buf[4];

   BYTE b1 = c >> 4;
   BYTE b2 = c & 0x0f;

   _snprintf(buf, 3, "%x", b1 + basechar1);
   csOut+="\\x";
   csOut+=buf;

   _snprintf(buf, 3, "%x", b2 + basechar1);
   csOut+="\\x";
   csOut+=buf;
}

/////////////////////////////////////////////////////////////////

CString Encrypt(const CString &csIn, const char *pPass)
{
   CString csOut;

   // initialize out 
   CCXRIntEnc sap((const BYTE*)pPass, strlen(pPass));

   /* 
      start each string with a random char.
      because this is a stream cipher, the ciphertext of a
      string like "ABC" will be the same as the first 3 bytes
      of the ciphertext for "ABCDEFG". 

      by starting with a random value, the cipher will be in a 
      different state (255:1) when it starts the real text. the
      decoder will simply discard the first plaintext byte.
   */ 
   BYTE seed = rand() % 256;
   BYTE c = sap.ProcessByte((BYTE)(seed));
   AddEncByte(c, csOut);

   // encrypt and convert to hex string
   for (int i=0; i < csIn.GetLength(); i++)
   {
      char t = csIn.GetAt(i);
      BYTE c = sap.ProcessByte((BYTE)(t));
      AddEncByte(c, csOut);
   }

   return csOut;
}


/////////////////////////////////////////////////////////////////

CString Decrypt(const char *pIn, const char *pPass)
{
   CString csOut;

   CCXRIntDec sap((const BYTE *)pPass, strlen(pPass));

   int iLen = _tcslen(pIn);

   if (iLen > 2)
   {
      int iBufLen = strlen(pIn);
      if (iBufLen & 0x01)
      {
         cerr << "Illegal string length in Decrypt\n";
         return pIn;
      }

      iBufLen/=2;

      for (int i=0;i<iBufLen;i++)
      {
         int b1 = pIn[i * 2] - basechar1;
         int b2 = pIn[i * 2 + 1] - basechar2;
         int c = (b1 << 4) | b2;

         BYTE bc = sap.ProcessByte((BYTE)(c));

         if (i>0) csOut+=(char)bc;
      }
   }

   return csOut;
}

/////////////////////////////////////////////////////////////////

bool AddDecode(const CString & csPassword, CStdioFile &out)
{
   out.WriteString(_T("\n\n/////////////////////////////////////////////////////////////\n"));
   out.WriteString(_T("// CXR-generated decoder follows\n\n"));
   out.WriteString(_T("#include <algorithm>\n"));
   out.WriteString(_T("const char * __pCXRPassword = \""));  

   // the password that encrypted the text used the literal text from the file (non-escaped \ chars).
   // we need to make sure that compiler sees the same text when it gets the passowrd. so,
   // we must double any "\" chars, to prevent them from becoming C-style escapes.
   out.WriteString(EscapeCString(csPassword));

   out.WriteString(_T("\";\n"));
   CString t; 
   t.Format("const int __iCXRDecBase1 = %d;\nconst int __iCXRDecBase2 = %d;\n\n", basechar1, basechar2);
   out.WriteString(t);
                            
   // the high-level decoding function
const char *pDec1 = 
"CString __CXRDecrypt(const char *pIn)\n"\
"{\n"\
"   CString x;char b[3];b[2]=0;\n"\
"   CXRD sap((const BYTE*)__pCXRPassword, strlen(__pCXRPassword));\n"\
"   int iLen = strlen(pIn);\n"\
"   if (iLen > 2)\n"\
"   {\n"\
"      int ibl=strlen(pIn);\n"\
"      if (ibl&0x01)\n"\
"      {\n"\
"         ASSERT(!\"Illegal string length in Decrypt\");\n"\
"         return pIn;\n"\
"      }\n"\
"      ibl/=2;\n"\
"      for (int i=0;i<ibl;i++)\n"\
"      {\n"\
"         int b1 =pIn[i*2]-__iCXRDecBase1;int b2=pIn[i*2+1]-__iCXRDecBase2;\n"\
"         int c = (b1 << 4) | b2; char ch =(char)(sap.pb((BYTE)(c)));\n"\
"         if (i>0) x+=ch;\n"\
"      }\n"\
"   }\n"\
"   return x;\n"\
"}\n";
 
   // the stream cipher
   const char *pStr1 =
"class CCXR\n" \
"{\n" \
"protected:\n" \
"   CCXR(const BYTE *key, unsigned int ks)\n" \
"   {\n" \
"      int i;BYTE rs;unsigned kp;\n" \
"      for(i=0;i<256;i++)c[i]=i;kp=0;rs=0;for(i=255;i;i--)std::swap(c[i],c[kr(i,key,ks,&rs,&kp)]);r2=c[1];r1=c[3];av=c[5];lp=c[7];lc=c[rs];rs=0;kp=0;\n" \
"   }\n" \
"	inline void SC(){BYTE st=c[lc];r1+=c[r2++];c[lc]=c[r1];c[r1]=c[lp];c[lp]=c[r2];c[r2]=st;av+=c[st];}\n" \
"	BYTE c[256],r2,r1,av,lp,lc;    \n" \
"\n" \
"   BYTE kr(unsigned int lm, const BYTE *uk, BYTE ks, BYTE *rs, unsigned *kp)\n" \
"   {\n" \
"      unsigned rl=0,mk=1,u;while(mk<lm)mk=(mk<<1)+1;do{*rs=c[*rs]+uk[(*kp)++];if(*kp>=ks){*kp=0;*rs+=ks;}u=mk&*rs;if(++rl>11)u%=lm;}while(u>lm);return u;\n" \
"   }\n" \
"};\n" \
"struct CXRD:CCXR\n" \
"{\n" \
"	CXRD(const BYTE *userKey, unsigned int keyLength=16) : CCXR(userKey, keyLength) {}\n" \
"	inline BYTE pb(BYTE b){SC();lp=b^c[(c[r1]+c[r2])&0xFF]^c[c[(c[lp]+c[lc]+c[av])&0xFF]];lc=b;return lp;}\n" \
"};\n";

   out.WriteString(pStr1);
   out.WriteString(pDec1);

   return true;
}

/////////////////////////////////////////////////////////////////

CString TranslateCString(const CString &csIn)
{
   // translate C-style string escapes as documented in K&R 2nd, A2.5.2

   CString csOut;

   for (int i=0;i<csIn.GetLength(); i++)
   {
      int c = csIn.GetAt(i);
      switch (c)
      {
      default:
         // normal text
         csOut+=c;
         break;
         // c-style escape
      case _T('\\'):
         if (i < csIn.GetLength() - 1)
         {
            c = csIn.GetAt(i + 1);
            switch (c)
            {
            case _T('n'):
               csOut+=_T('\n');
               break;
            case _T('t'):
               csOut+=_T('\t');
               break;
            case _T('v'):
               csOut+=_T('\v');
               break;
            case _T('b'):
               csOut+=_T('\b');
               break;
            case _T('r'):
               csOut+=_T('\r');
               break;
            case _T('f'):
               csOut+=_T('\f');
               break;
            case _T('a'):
               csOut+=_T('\a');
               break;
            case _T('\\'):
               csOut+=_T('\\');
               break;
            case _T('?'):
               csOut+=_T('?');
               break;
            case _T('\''):
               csOut+=_T('\'');
               break;
            case _T('\"'):
               csOut+=_T('\"');
               break;
            case _T('0'):
            case _T('1'):
            case _T('2'):
            case _T('3'):
            case _T('4'):
            case _T('5'):
            case _T('6'):
            case _T('7'):
               {
                  // expand octal
                  int iConsumed = 0;
                  if (!ExpandOctal(csIn.Mid(i), csOut, iConsumed))
                  {
                     cerr << _T("Invalid octal sequence: ") << _T('\"') << (LPCTSTR)csIn << _T('\"') << endl;
                     csOut = csIn;
                     break;
                  }

                  i+=iConsumed - 1;
               }
               break;
            case _T('x'):
               { 
                  // expand hex
                  int iConsumed = 0;
                  if (!ExpandHex(csIn.Mid(i), csOut, iConsumed))
                  {
                     cerr << _T("Invalid hex sequence: ") << _T('\"') << (LPCTSTR)csIn << _T('\"') << endl;
                     csOut = csIn;
                     break;
                  }

                  i+=iConsumed - 1;

               }
               break;
            }

            i++;
            continue;
         }
         else
         {
            cerr << _T("Invalid escape sequence: ") << _T('\"') << (LPCTSTR)csIn << _T('\"') << endl;
            csOut = csIn;
            break;
         }
         break;
      }
   }

   return csOut;
}

/////////////////////////////////////////////////////////////////

bool ExpandOctal(const CString &csIn, CString &csOut, int &iConsumed)
{
   // staring with the escape, we need at least one more char
   if (csIn.GetLength() < 2)
   {
      return false;
   }

   if (csIn.GetAt(0) != _T('\\'))
   {
      return false;
   }

   int iStart = 1;
   int iCur = iStart;

   CString digits;
   int c = csIn.GetAt(iCur);
   while ((c >= _T('0')) && (c <= _T('7')))
   {
      digits+=c;

      // an escape can't hold more that 3 octal digits (K&R 2nd A2.5.2)
      if (iCur == 3)
      {
         break;
      }
         
      iCur++;
      c = csIn.GetAt(iCur);
   }

   char *end;
   int octval = (char)_tcstol(digits, &end, 8);

   iConsumed = digits.GetLength();

   csOut+=octval;

   return true;
}

/////////////////////////////////////////////////////////////////

bool ExpandHex(const CString &csIn, CString &csOut, int &iConsumed)
{
   // staring with the escape and the 'x', we need at least one more char
   if (csIn.GetLength() < 3)
   {
      return false;
   }

   if ((csIn.GetAt(0) != _T('\\')) || (csIn.GetAt(1) != _T('x')))
   {
      return false;
   }

   int iStart = 2;
   int iCur = iStart;

   CString digits;
   int c = csIn.GetAt(iCur);
   while (_istxdigit(c))
   {
      digits+=c;

      iCur++;
      c = csIn.GetAt(iCur);
   }

   char *end;

   // "There is no limit on the number of digits, but the behavior is undefined
   // if the resulting character value exceeds that of the largest character"
   // (K&R 2nd A2.5.2)
   int hex = (char)_tcstol(digits, &end, 16);

   iConsumed = digits.GetLength();

   iConsumed++; // count the "x"

   csOut+=hex;

   return true;
}

/////////////////////////////////////////////////////////////////

CString EscapeCString(const char *pIn)
{
   CString csOut;

   int iLen = _tcslen(pIn);

   for (int i=0;i<iLen;i++)
   {
      csOut+=pIn[i];
      // double all "\" chars
      if (pIn[i] == _T('\\'))
      {
         csOut+=_T('\\');
      }
   }

   return csOut;
}

/////////////////////////////////////////////////////////////////
