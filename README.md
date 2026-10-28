# Port of Strongtalk to MacOS Apple Silicon
## An AI Coding Experiment with Claude
### A High-Performance Open Source Smalltalk With An Optional Type System

Strongtalk is an experimental Smalltalk implementation from 1990's with optional type system.  You can learn more about it at the [official website](https://strongtalk.org).  As far as I can see the project is abandoned.  The last commit at their [GitHub archive](http://github.com/talksmall/Strongtalk) was sixteen years ago. 

I've long been curious about this mythical super high speed Smalltalk with optional typing but it wasn't finished, and it was Win32 dependent and I'm not JIT/VM wizard, I'm an app/architect guy.

Anyhow, I was hearing from the AI fans that AI really can do advanced software development these days and so I thought I would see how advanced.

Conclusion - pretty dang advanced.  All I did is download this code from the Strongtalk website as an archive (I found the git repo later), start and Xcode project with a bunch of the source, and engage Claude's spiffy new model   It took about a week to get to this point, only because I kept running out of session tokens.  I think it could have been done in a couple days with unlimited tokens and Coca-Cola.  So for me it wasn't about Strongtalk so much as testing out Claude.  But I realize other people are interested.  So here it is.  An Xcode project, sources, etc. Working on Apple silicon with the GUI now hosted on Cocoa. There is a plain Strongtalk debug build target - it uses the interpreter.  The Strongtalk (Release) target engages the JIT.  I suggest seatbelts because the speed up is about 10-20x.  The rest is replicated from the website to save you a click.  I guess coding really is over.

Quoting from that page:

### What Is Strongtalk?

Strongtalk is a major re-thinking of the Smalltalk-80 programming language and system. While retaining the basic Smalltalk syntax and semantics, it contains a number of significant advances, including:

  * **Performance**: It executes Smalltalk much faster than any other Smalltalk implementation, using an advanced inlining compiler based on type-feedback technology originally developed at Sun Microsystem Labs. Remarkably, code is dynamically compiled and decompiled as necessary, even while it is running, in order to maintain perfect interpreted semantics. Code can be transparently debugged and changed on-the-fly, yet will still run at full speed.

  * **Type System**: It contains the first fully developed strong, static type system for Smalltalk (hence the name Strongtalk). The type system is both optional and incremental, and operates completely independently of the compiler technology (which means that normal untyped Smalltalk code runs just as fast as typed code). It also contains a re-designed, strongly-typed “Blue Book” class library.

The Strongtalk system was developed in secret in the mid-90’s by a small startup company. Before the Strongtalk system could be released, the company was acquired by Sun Microsystems, Inc. to work on the Java® virtual machine. Development of Strongtalk was halted at that point, so very few people have ever had a chance to see the Strongtalk system in action. This is highly unfortunate, since Smalltalk is still a more elegant and advanced programming language than any existing mainstream programming language, and Strongtalk is by far the fastest implementation of Smalltalk ever.

Fortunately, Sun Microsystems has graciously released Strongtalk as completely free, open-source software. The first release from Sun in 2002 did not include source code for the virtual-machine (which contains the compiler). Only the binary was released, rendering the system unsuitable for any serious use, other than evaluation by researchers. As of September 2006, this has been remedied with the new release, which contains full source.

Strongtalk development was halted before it was fully productized, so considerable work remains to polish-up Strongtalk before it is ready to be used in real-world applications. But with the release of the virtual-machine source code, a whole new world of possibilities has opened up. What will become of Strongtalk? Now, the answer is finally in the community’s hands!

®Java is a trademark of Sun Microsystems, Inc.

Strongtalk is free, open-source software released under a BSD-style license.
