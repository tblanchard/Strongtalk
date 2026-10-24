"Corrections of the 32-bit Strongtalk image for the 64-bit VM. The VM files this in when the
 image first reads its command line (image_patches.st in the current directory, or
 $STRONGTALK_PATCH_FILE; see file_in_image_patches in system_prims.cpp). +PrintImagePatches
 reports it. Chunk format: every chunk, this comment included, ends with a bang."!

! (Delta mirrorFor: #SmallInteger) methodsFor: 'private' !

digitsForAbs: v <Int> ^<Int>
	"The number of decimal digits of v (>= 0). SmallIntegers have up to 19 digits on 64-bit
	 (the 32-bit version stopped at 10)."

	| n <Int> p <Int> |
	n := 1.
	p := 10.
	[ n < 19 and: [ v >= p ] ] whileTrue: [ n := n + 1. p := p * 10 ].
	^n! !


! (Delta mirrorFor: #DeltaRewriter) methodsFor: 'Unclassified' !

normalSendNode: node
	| recv msg sel arg1 arg2 arg3 |
	self assert: [ node isNormalSendNode ].
	recv := self rewrite: node recv.

	recv isFloatValue ifTrue: [
		result := self rewrite: node asFloatOperationOn: recv.
		result notNil ifTrue: [ ^ self ].
		"no FloatValue operation found"
		AutoFloatConversion
			ifTrue: [ recv := DeltaFloatUnaryOpToOop new f: #asFloat x: recv ]
			ifFalse: [ self at: node error: 'not a supported FloatValue operation' ]
	].

	"Prepare arguments for normal message sends"
	msg := self rewriteAsOop: node msg.
	sel := msg selector.
	msg nofArgs >= 1 ifTrue: [ arg1 := msg args at: 1 ].
	msg nofArgs >= 2 ifTrue: [ arg2 := msg args at: 2 ].
	msg nofArgs >= 3 ifTrue: [ arg3 := msg args at: 3 ].

	"Self & Super sends"
	(sel ~= #== and: [ sel ~= #~~ ]) ifTrue: [
		(recv isRefNode and: [ recv decl isReceiverNode ])	ifTrue: [ result := DeltaSelfSend new send: msg.						^ self ]
	].
	(recv isRefNode and: [ recv decl isSuperVarNode ])		ifTrue: [ result := DeltaSuperSend new send: msg.					^ self ].

	"Hardwired asFloatValue message"
	sel = #asFloatValue ifTrue: [
		result :=
			EnableFloatValues
				ifTrue: [ DeltaFloatify new obj: recv ]
				ifFalse: [ recv "simply ignore conversion" ].
		^ self
	].

	"Hardwired ifTrue: / ifFalse: messages"
	self if: [ sel = #ifTrue:				] with: arg1 nofArgs: 0	do: [ result := DeltaIf new if: recv is: true then: arg1.						^ self ].
	self if: [ sel = #ifFalse:			] with: arg1 nofArgs: 0	do: [ result := DeltaIf new if: recv is: false then: arg1.					^ self ].
	self if: [ sel = #ifTrue:ifFalse:	] with: arg1 and: arg2	do: [ result := DeltaIf new if: recv is: true then: arg1 else: arg2.	^ self ].
	self if: [ sel = #ifFalse:ifTrue:	] with: arg1 and: arg2	do: [ result := DeltaIf new if: recv is: false then: arg1 else: arg2.	^ self ].

	"Hardwired and: / or: messages"
	self if: [ sel = #and:					] with: arg1 nofArgs: 0	do: [ result := DeltaAndOr new if: recv and: arg1.							^ self ].
	self if: [ sel = #or:					] with: arg1 nofArgs: 0	do: [ result := DeltaAndOr new if: recv or: arg1.								^ self ].

	"Hardwired whileTrue: / whileFalse: messages"
	self if: [ sel = #whileTrue			] with: recv nofArgs: 0	do: [ result := DeltaLoop new while: true do: recv.							^ self ].
	self if: [ sel = #whileFalse		] with: recv nofArgs: 0	do: [ result := DeltaLoop new while: false do: recv.						^ self ].
	self if: [ sel = #whileTrue:		] with: recv and: arg1	do: [ result := DeltaLoop new while: recv is: true do: arg1.			^ self ].
	self if: [ sel = #whileFalse:		] with: recv and: arg1	do: [ result := DeltaLoop new while: recv is: false do: arg1.			^ self ].

	"Hardwired repeat: / timesRepeat: / to:do: / to:by:do: messages"
	self if: [ sel = #repeat				] with: recv nofArgs: 0	do: [ result := DeltaLoop new repeat: recv.										^ self ].
	self if: [ sel = #timesRepeat:	] with: arg1 nofArgs: 0	do: [ result := DeltaLoop new do: recv timesRepeat: arg1.			^ self ].
	self if: [ sel = #to:do:				] with: arg2 nofArgs: 1	do: [ result := DeltaLoop new from: recv to: arg1 do: arg2.			^ self ].
	"inline only if step is a constant (64-bit VM patch: tested in the condition - the action runs
	 after the block's scope has been marked inlined, so a variable step left a send with an inlined block)"
	self if: [ sel = #to:by:do: and: [ arg2 isIntNode ] ] with: arg3 nofArgs: 1 do: [
		result := DeltaLoop new from: recv to: arg1 by: arg2 do: arg3.
		^ self
	].

	"Hardwired value message"
"
	self if: [ sel = #value				] with: recv nofArgs: 0 do: [ result := DeltaInlinedBlock new block: recv.							^ self ].
"

	"No rewriting"
	result := DeltaNormalSend new to: recv send: msg! !

"TypedCodeView>>showType called showSourceError:at:, which does not exist (a doesNotUnderstand:
 walkback when the selection does not parse as a method): show the parse error as the other views"!

! (Delta mirrorFor: #TypedCodeView) methodsFor: 'control' !

showType

	| 
		mirror <Mirror>   selection <Tuple[Int, Int]> node <ASTNode> parseTree <ParseTree> 
		position <Tuple[Int, Int]>   otype <ObjectType> eh <TypeErrorHandler>
	|
	
	mirror := self codeManager mirror.
	selection := self linearSelection.
	selection at1 = 0 ifTrue: [^self].
	parseTree := DeltaParser new parseMethod: self modelString within: mirror scope ifError: [ :msg :pos | self showParseError: msg at: pos in: self. ^ self].
	node := parseTree nodeForPosition: selection.
	position := parseTree positionForNode: node.	
	self  sourceSelection: position.
	eh := Type doChecks: [otype := mirror typeForNode: node inMethod: parseTree body].					
	eh reportedErrors isEmpty
		ifTrue:[
						self visual promptOk:  'Type of selection is ', otype name asString printString
											title: 'Showing type in ', self codeManager locator name
											type: #info
											action: [].
					] 
		ifFalse: [
                         otype isError  ifTrue:[
                         											self reportShowTypeResult: 'Selection has erroneous type. Browse type errors?'
                         													errors: eh reportedErrors
                         										]
                                                     ifFalse:[
                                                     				self reportShowTypeResult:  'Type of selection is ' ,
                                                                                otype name asString printString, '; however the expression includes type errors. Browse type errors?'
                                                                            errors: eh reportedErrors
                                                                   ].
                          ].
! !
