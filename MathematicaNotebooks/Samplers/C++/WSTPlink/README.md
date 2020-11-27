
This is the WSTPLink.
Compile it as for the below outside Mathematica 
then link it or with a sys port(by running it in a CLI) or 
directly in Mathematica (info below) 

It's all nice and such :) but take care than this __won't__ work with Parallelize or any other ParallelXXX stuff ! :/

-

First, do

$ PATH=$PATH:/opt/Wolfram/Mathematica/11.2/SystemFiles/Links/WSTP/DeveloperKit/Linux-x86-64/CompilerAdditions
$ export PATH

Then 

$ wscc addthree.tm -o addthree
(wscc is the new mcc .. don't use mcc!)

Or with a .c and a .tm files 

$ wsprep randomRealHalton.cxx randomRealHalton.tm -o randomRealHaltontm.c
$ wscc randomRealHaltontm.c -o randomRealHalton



Eventually in Mathematica
-------------------------

SetDirectory[$InstallationDirectory <> "/SystemFiles/Links/WSTP/DeveloperKit/" <> $SystemID <> "/PrebuiltExamples"]
link = Install["./addthree"]
LinkPatterns[link]


