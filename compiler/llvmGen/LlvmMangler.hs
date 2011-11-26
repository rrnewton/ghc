{-# OPTIONS -fno-warn-unused-binds #-}
-- -----------------------------------------------------------------------------
-- | GHC LLVM Mangler
--
-- This script processes the assembly produced by LLVM, rearranging the code
-- so that an info table appears before its corresponding function.
--
-- On OSX we also use it to fix up the stack alignment, which needs to be 16
-- byte aligned but always ends up off by word bytes because GHC sets it to
-- the 'wrong' starting value in the RTS.
--

module LlvmMangler ( llvmFixupAsm ) where

#include "HsVersions.h"

import LlvmCodeGen.Ppr ( infoSection )

import Control.Exception
import Control.Monad ( when )
import qualified Data.ByteString.Char8 as B
import Data.Char
import System.IO

import Data.List ( sortBy )
import Data.Function ( on )

-- Magic Strings
secStmt, infoSec, newLine, spInst, jmpInst, textStmt, dataStmt, syntaxUnified :: B.ByteString
secStmt    = B.pack "\t.section\t"
infoSec    = B.pack infoSection
newLine    = B.pack "\n"
jmpInst    = B.pack "\n\tjmp"
textStmt   = B.pack "\t.text"
dataStmt   = B.pack "\t.data"
syntaxUnified = B.pack "\t.syntax unified"

infoLen, labelStart, spFix :: Int
infoLen    = B.length infoSec
labelStart = B.length jmpInst

#if x86_64_TARGET_ARCH
spInst     = B.pack ", %rsp\n"
spFix      = 8
#else
spInst     = B.pack ", %esp\n"
spFix      = 4
#endif

-- Search Predicates
eolPred, dollarPred, commaPred :: Char -> Bool
eolPred    = ((==) '\n')
dollarPred = ((==) '$')
commaPred  = ((==) ',')

-- | Read in assembly file and process
llvmFixupAsm :: FilePath -> FilePath -> IO ()
llvmFixupAsm f1 f2 = do
    r <- openBinaryFile f1 ReadMode
    w <- openBinaryFile f2 WriteMode
    ss <- readSections r w
    hClose r
#if mingw32_TARGET_OS
    let fixed = (map fixMovaps . fixTables) ss
#else
    let fixed = fixTables ss
#endif
    mapM_ (writeSection w) fixed
    hClose w
    return ()

type Section = (B.ByteString, B.ByteString)

-- | Splits the file contents into its sections. Each is returned as a
-- pair of the form (header line, contents lines)
readSections :: Handle -> Handle -> IO [Section]
readSections r w = go B.empty [] []
  where
    go hdr ss ls = do
      e_l <- (try (B.hGetLine r))::IO (Either IOError B.ByteString)

      -- Note that ".type" directives at the end of a section refer to
      -- the first directive of the *next* section, therefore we take
      -- it over to that section.
      let (tys, ls') = span isType ls
          isType = B.isPrefixOf (B.pack "\t.type")
          cts = B.intercalate newLine $ reverse ls'

      -- Decide whether to directly output the section or append it
      -- to the list for resorting.
      let finishSection
            | infoSec `B.isInfixOf` hdr =
                cts `seq` return $ (hdr, cts):ss
            | otherwise =
                writeSection w (hdr, fixupStack cts B.empty) >> return ss

      case e_l of
        Right l | l == syntaxUnified 
                  -> finishSection >>= \ss' -> writeSection w (l, B.empty)
                                   >> go B.empty ss' tys
                | any (`B.isPrefixOf` l) [secStmt, textStmt, dataStmt]
                  -> finishSection >>= \ss' -> go l ss' tys
                | otherwise
                  -> go hdr ss (l:ls)
        Left _    -> finishSection >>= \ss' -> return (reverse ss')

-- | Writes sections back
writeSection :: Handle -> Section -> IO ()
writeSection w (hdr, cts) = do
  when (not $ B.null hdr) $
    B.hPutStrLn w hdr
  B.hPutStrLn w cts

fixMovaps :: Section -> Section
fixMovaps (hdr, cts) =
    (hdr, loop idxs cts)
  where
    loop :: [Int] -> B.ByteString -> B.ByteString
    loop [] cts = cts
                  
    loop (i : is) cts =
        loop is (hd `B.append` movups `B.append` B.drop 6 tl)
      where
        (hd, tl) = B.splitAt i cts

    idxs :: [Int]
    idxs = B.findSubstrings movaps cts

    movaps, movups :: B.ByteString
    movaps = B.pack "movaps"
    movups = B.pack "movups"

-- | Reorder and convert sections so info tables end up next to the
-- code. Also does stack fixups.
fixTables :: [Section] -> [Section]
fixTables ss = fixed
  where
    -- Resort sections: We only assign a non-zero number to all
    -- sections having the "STRIP ME" marker. As sortBy is stable,
    -- this will cause all these sections to be appended to the end of
    -- the file in the order given by the indexes.
    extractIx hdr
      | B.null a  = 0
      | otherwise = 1 + readInt (B.takeWhile isDigit $ B.drop infoLen a)
      where (_,a) = B.breakSubstring infoSec hdr
    indexed = zip (map (extractIx . fst) ss) ss
    sorted = map snd $ sortBy (compare `on` fst) indexed

    -- Turn all the "STRIP ME" sections into normal text sections, as
    -- they are in the right place now.
    strip (hdr, cts)
      | infoSec `B.isInfixOf` hdr = (textStmt, cts)
      | otherwise                 = (hdr, cts)
    stripped = map strip sorted

    -- Do stack fixup
    fix (hdr, cts) = (hdr, fixupStack cts B.empty)
    fixed = map fix stripped
 
{-|
    Mac OS X requires that the stack be 16 byte aligned when making a function
    call (only really required though when making a call that will pass through
    the dynamic linker). The alignment isn't correctly generated by LLVM as
    LLVM rightly assumes that the stack will be aligned to 16n + 12 on entry
    (since the function call was 16 byte aligned and the return address should
    have been pushed, so sub 4). GHC though since it always uses jumps keeps
    the stack 16 byte aligned on both function calls and function entry.

    We correct the alignment here for Mac OS X i386. The x86_64 target already
    has the correct alignment since we keep the stack 16+8 aligned throughout
    STG land for 64-bit targets.
-}
fixupStack :: B.ByteString -> B.ByteString -> B.ByteString

#if !darwin_TARGET_OS || x86_64_TARGET_ARCH
fixupStack = const

#else
fixupStack f f' | B.null f' =
    let -- fixup sub op
        (a, c) = B.breakSubstring spInst f
        (b, n) = B.breakEnd dollarPred a
        num    = B.pack $ show $ readInt n + spFix
    in if B.null c
          then f' `B.append` f
          else fixupStack c $ f' `B.append` b `B.append` num

fixupStack f f' =
    let -- fixup add ops
        (a, c)  = B.breakSubstring jmpInst f
        -- we matched on a '\n' so go past it
        (l', b) = B.break eolPred $ B.tail c
        l       = (B.head c) `B.cons` l'
        (a', n) = B.breakEnd dollarPred a
        (n', x) = B.break commaPred n
        num     = B.pack $ show $ readInt n' + spFix
        -- We need to avoid processing jumps to labels, they are of the form:
        -- jmp\tL..., jmp\t_f..., jmpl\t_f..., jmpl\t*%eax..., jmpl *L...
        targ = B.dropWhile ((==)'*') $ B.drop 1 $ B.dropWhile ((/=)'\t') $
                B.drop labelStart c
    in if B.null c
          then f' `B.append` f
          else if B.head targ == 'L'
                then fixupStack b $ f' `B.append` a `B.append` l
                else fixupStack b $ f' `B.append` a' `B.append` num `B.append`
                                    x `B.append` l
#endif

-- | Read an int or error
readInt :: B.ByteString -> Int
readInt str | B.all isDigit str = (read . B.unpack) str
            | otherwise = error $ "LLvmMangler Cannot read " ++ show str
                                ++ " as it's not an Int"

