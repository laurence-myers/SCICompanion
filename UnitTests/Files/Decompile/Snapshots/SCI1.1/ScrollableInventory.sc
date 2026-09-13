;;; Sierra Script 1.0 - (do not remove this comment)
(script# 26)
(include sci.sh)
(use Main)
(use Print)
(use IconItem)
(use SysWindow)
(use InventoryItem)
(use System)


(local
	local0
)
(procedure (localproc_00de param1 param2 param3 &tmp temp0 temp1 temp2)
	(= temp2
		(+
			(/ (- (param1 nsRight?) (param1 nsLeft?)) 2)
			(param1 nsLeft?)
		)
	)
	(= temp1 param2)
	(while (>= (Abs (- temp1 param3)) 4)
		(= temp0
			(self
				firstTrue: 226 (((gUser curEvent?) new:) x: temp2 y: temp1 yourself:)
			)
		)
		(if temp0 (return))
		(if (< param2 param3) (+= temp1 4) else (-= temp1 4))
	)
)

(class InventoryBase of IconBar
	(properties
		elements 0
		size 0
		height 0
		underBits 0
		oldMouseX 0
		oldMouseY 0
		curIcon 0
		highlightedIcon 0
		prevIcon 0
		curInvIcon 0
		useIconItem 0
		helpIconItem 0
		walkIconItem 0
		port 0
		window 0
		state 1024
		activateHeight 0
		y 0
		normalHeading {You are carrying:}
		heading 0
		empty {nothing!}
		iconBarInvItem 0
		okButton 0
		selectIcon 0
	)
	
	(method (init)
		(= heading normalHeading)
	)
	
	(method (doit &tmp temp0 temp1 temp2 temp3 temp4 [temp5 2] temp7 temp8 temp9 [temp10 51])
		(asm
code_015e:
			pushi    #type
			pushi    0
			pushi    #new
			pushi    0
			pushi    #curEvent
			pushi    0
			lag      gUser
			send     4
			send     4
			sat      temp1
			send     4
			bnt      code_0179
			jmp      code_015e
code_0179:
			pTos     state
			ldi      32
			and     
			bnt      code_060d
			pushi    #new
			pushi    0
			pushi    #curEvent
			pushi    0
			lag      gUser
			send     4
			send     4
			sat      temp1
			pushi    #x
			pushi    0
			send     4
			sag      gPEventX
			pushi    #y
			pushi    0
			lat      temp1
			send     4
			sag      gPEventY
			pushi    #type
			pushi    0
			lat      temp1
			send     4
			sat      temp2
			pushi    #message
			pushi    0
			lat      temp1
			send     4
			sat      temp3
			pushi    #modifiers
			pushi    0
			lat      temp1
			send     4
			sat      temp4
			ldi      0
			sat      temp9
			pushi    #localize
			pushi    0
			lat      temp1
			send     4
			pToa     curIcon
			bnt      code_0235
			lat      temp4
			not     
			bnt      code_0235
			pTos     curIcon
			pToa     selectIcon
			ne?     
			bnt      code_0235
			lst      temp2
			ldi      1
			eq?     
			bt       code_0206
			lst      temp2
			ldi      4
			eq?     
			bnt      code_01fa
			lst      temp3
			ldi      13
			eq?     
			bnt      code_01fa
			ldi      1
			sat      temp9
			bt       code_0206
code_01fa:
			lst      temp2
			ldi      256
			eq?     
			bnt      code_0206
			ldi      1
			sat      temp9
code_0206:
			bnt      code_0235
			pTos     curIcon
			pToa     helpIconItem
			ne?     
			bt       code_021b
			pushi    #signal
			pushi    0
			pToa     helpIconItem
			send     4
			push    
			ldi      16
			and     
code_021b:
			bnt      code_0235
			pushi    #type
			pushi    1
			pushi    16384
			pushi    37
			pushi    1
			pushi    #message
			pushi    0
			pToa     curIcon
			send     4
			push    
			lat      temp1
			send     12
code_0235:
			pushi    1
			lst      temp1
			callk    MapKeyToDir,  2
			pushi    #type
			pushi    0
			lat      temp1
			send     4
			sat      temp2
			pushi    #message
			pushi    0
			lat      temp1
			send     4
			sat      temp3
			lag      gCuees
			bnt      code_025c
			pushi    #eachElementDo
			pushi    1
			pushi    57
			send     6
code_025c:
			lag      gFastCast
			bnt      code_026c
			pushi    #handleEvent
			pushi    1
			lst      temp1
			send     6
			jmp      code_0179
code_026c:
			lst      temp2
			ldi      1
			eq?     
			bnt      code_028b
			lat      temp4
			bnt      code_028b
			pushi    #advanceCurIcon
			pushi    0
			self     4
			pushi    #claimed
			pushi    1
			pushi    1
			lat      temp1
			send     6
			jmp      code_0179
code_028b:
			lst      temp2
			ldi      0
			eq?     
			bnt      code_02b3
			pushi    #firstTrue
			pushi    2
			pushi    226
			lst      temp1
			self     8
			sat      temp0
			bnt      code_02b3
			push    
			pToa     highlightedIcon
			ne?     
			bnt      code_02b3
			pushi    #highlight
			pushi    1
			lst      temp0
			self     6
			jmp      code_0179
code_02b3:
			lst      temp2
			ldi      1
			eq?     
			bt       code_02ce
			lst      temp2
			ldi      4
			eq?     
			bnt      code_02c8
			lst      temp3
			ldi      13
			eq?     
			bt       code_02ce
code_02c8:
			lst      temp2
			ldi      256
			eq?     
code_02ce:
			bnt      code_0368
			pushi    1
			pTos     highlightedIcon
			callk    IsObject,  2
			bnt      code_02f7
			pushi    184
			pushi    #view
			pTos     highlightedIcon
			lst      temp2
			ldi      1
			eq?     
			push    
			self     8
			bnt      code_02f7
			pTos     highlightedIcon
			pToa     okButton
			eq?     
			bnt      code_02fa
			jmp      code_060d
code_02f7:
			jmp      code_0179
code_02fa:
			pTos     highlightedIcon
			pToa     helpIconItem
			eq?     
			bnt      code_034f
			pushi    #cursor
			pushi    0
			pToa     highlightedIcon
			send     4
			push    
			ldi      65535
			ne?     
			bnt      code_0322
			pushi    #setCursor
			pushi    1
			pushi    #cursor
			pushi    0
			pToa     helpIconItem
			send     4
			push    
			lag      gGame
			send     6
code_0322:
			pTos     state
			ldi      2048
			and     
			bnt      code_0334
			pushi    #noClickHelp
			pushi    0
			self     4
			jmp      code_0179
code_0334:
			pToa     helpIconItem
			bnt      code_052d
			pushi    14
			pushi    #x
			pushi    #signal
			pushi    0
			send     4
			push    
			ldi      16
			or      
			push    
			pToa     helpIconItem
			send     6
			jmp      code_0179
code_034f:
			pToa     highlightedIcon
			aTop     curIcon
			pushi    #setCursor
			pushi    1
			pushi    #cursor
			pushi    0
			pToa     curIcon
			send     4
			push    
			lag      gGame
			send     6
			jmp      code_0179
code_0368:
			lst      temp2
			ldi      64
			and     
			bnt      code_0422
			lst      temp3
			dup     
			ldi      3
			eq?     
			bnt      code_0382
			pushi    #advance
			pushi    0
			self     4
			jmp      code_041e
code_0382:
			dup     
			ldi      7
			eq?     
			bnt      code_0392
			pushi    #retreat
			pushi    0
			self     4
			jmp      code_041e
code_0392:
			dup     
			ldi      1
			eq?     
			bnt      code_03ca
			pToa     highlightedIcon
			bnt      code_03c1
			pushi    3
			push    
			pushi    #nsTop
			pushi    0
			send     4
			push    
			ldi      1
			sub     
			push    
			pushi    0
			call     localproc_00de,  6
			sat      temp0
			bnt      code_03c1
			pushi    #highlight
			pushi    2
			lst      temp0
			pushi    1
			self     8
			jmp      code_041e
code_03c1:
			pushi    #retreat
			pushi    0
			self     4
			jmp      code_041e
code_03ca:
			dup     
			ldi      5
			eq?     
			bnt      code_040a
			pToa     highlightedIcon
			bnt      code_0401
			pushi    3
			push    
			pushi    #nsBottom
			pushi    0
			send     4
			push    
			ldi      1
			add     
			push    
			pushi    #bottom
			pushi    0
			pToa     window
			send     4
			push    
			call     localproc_00de,  6
			sat      temp0
			bnt      code_0401
			pushi    #highlight
			pushi    2
			lst      temp0
			pushi    1
			self     8
			jmp      code_041e
code_0401:
			pushi    #advance
			pushi    0
			self     4
			jmp      code_041e
code_040a:
			dup     
			ldi      0
			eq?     
			bnt      code_041e
			lst      temp2
			ldi      4
			and     
			bnt      code_041e
			pushi    #advanceCurIcon
			pushi    0
			self     4
code_041e:
			toss    
			jmp      code_0179
code_0422:
			lst      temp2
			ldi      4
			eq?     
			bnt      code_0457
			lst      temp3
			dup     
			ldi      9
			eq?     
			bnt      code_043a
			pushi    #advance
			pushi    0
			self     4
			jmp      code_0453
code_043a:
			dup     
			ldi      3840
			eq?     
			bnt      code_044a
			pushi    #retreat
			pushi    0
			self     4
			jmp      code_0453
code_044a:
			dup     
			ldi      27
			eq?     
			bnt      code_0453
			jmp      code_060d
code_0453:
			toss    
			jmp      code_0179
code_0457:
			lst      temp2
			ldi      16384
			and     
			bnt      code_052d
			pushi    #firstTrue
			pushi    2
			pushi    226
			lst      temp1
			self     8
			sat      temp0
			bnt      code_052d
			lst      temp2
			ldi      8192
			and     
			bnt      code_0523
			lat      temp0
			bnt      code_04fe
			pushi    #noun
			pushi    0
			send     4
			bnt      code_04fe
			pushi    7
			pushi    0
			pushi    #modNum
			pushi    0
			lat      temp0
			send     4
			push    
			pushi    #noun
			pushi    0
			lat      temp0
			send     4
			push    
			pushi    #helpVerb
			pushi    0
			lat      temp0
			send     4
			push    
			pushi    0
			pushi    1
			lea      @temp10
			push    
			callk    Message,  14
			bnt      code_04fe
			pushi    #respondsTo
			pushi    1
			pushi    244
			lag      gWindow
			send     6
			bnt      code_04f2
			pushi    #eraseOnly
			pushi    0
			lag      gWindow
			send     4
			sat      temp7
			pushi    #eraseOnly
			pushi    1
			pushi    1
			lag      gWindow
			send     6
			pushi    1
			lea      @temp10
			push    
			calle    Prints,  2
			pushi    #eraseOnly
			pushi    1
			lst      temp7
			lag      gWindow
			send     6
			jmp      code_04fe
code_04f2:
			pushi    1
			lea      @temp10
			push    
			calle    Prints,  2
code_04fe:
			pushi    14
			pushi    #x
			pushi    #signal
			pushi    0
			pToa     helpIconItem
			send     4
			push    
			ldi      65519
			and     
			push    
			pToa     helpIconItem
			send     6
			pushi    #setCursor
			pushi    1
			pushi    999
			lag      gGame
			send     6
			jmp      code_0179
code_0523:
			lst      temp0
			pToa     okButton
			eq?     
			bnt      code_0530
			jmp      code_060d
code_052d:
			jmp      code_0179
code_0530:
			pushi    #isKindOf
			pushi    1
			class    InventoryItem
			push    
			lat      temp0
			send     6
			not     
			bnt      code_0596
			pushi    #select
			pushi    2
			lst      temp0
			lat      temp9
			not     
			push    
			self     8
			bnt      code_0179
			lat      temp0
			aTop     curIcon
			pushi    #setCursor
			pushi    1
			pushi    #cursor
			pushi    0
			pToa     curIcon
			send     4
			push    
			lag      gGame
			send     6
			lst      temp0
			pToa     helpIconItem
			eq?     
			bnt      code_0179
			pTos     state
			ldi      2048
			and     
			bnt      code_057e
			pushi    #noClickHelp
			pushi    0
			self     4
			jmp      code_0179
code_057e:
			pushi    14
			pushi    #x
			pushi    #signal
			pushi    0
			pToa     helpIconItem
			send     4
			push    
			ldi      16
			or      
			push    
			pToa     helpIconItem
			send     6
			jmp      code_0179
code_0596:
			pToa     curIcon
			bnt      code_0179
			pushi    #respondsTo
			pushi    1
			pushi    244
			lag      gWindow
			send     6
			bnt      code_05be
			pushi    #eraseOnly
			pushi    0
			lag      gWindow
			send     4
			sat      temp7
			pushi    #eraseOnly
			pushi    1
			pushi    1
			lag      gWindow
			send     6
code_05be:
			pushi    #isKindOf
			pushi    1
			class    InventoryItem
			push    
			pToa     curIcon
			send     6
			bnt      code_05df
			pushi    #doVerb
			pushi    1
			pushi    #message
			pushi    0
			pToa     curIcon
			send     4
			push    
			lat      temp0
			send     6
			jmp      code_05f1
code_05df:
			pushi    #doVerb
			pushi    1
			pushi    #message
			pushi    0
			lat      temp1
			send     4
			push    
			lat      temp0
			send     6
code_05f1:
			pushi    #respondsTo
			pushi    1
			pushi    244
			lag      gWindow
			send     6
			bnt      code_0179
			pushi    #eraseOnly
			pushi    1
			lst      temp7
			lag      gWindow
			send     6
			jmp      code_0179
code_060d:
			pushi    #hide
			pushi    0
			self     4
			ret     
		)
	)
	
	(method (showSelf param1)
		(gSounds pause:)
		(if
		(and gPseudoMouse (gPseudoMouse respondsTo: 173))
			(gPseudoMouse stop:)
		)
		(if (gIconBar height?) (gIconBar hide:))
		(if (not window) (= window (SysWindow new:)))
		(if (window window?) (window dispose:) (= window 0))
		(if (not okButton)
			(= okButton (NodeValue (self first:)))
		)
		(= curIcon 0)
		(if (self show: (if argc param1 else gEgo))
			(self doit:)
		)
	)
	
	(method (show param1 &tmp temp0 temp1)
		(gGame
			setCursor: (if curIcon (curIcon cursor?) else (selectIcon cursor?))
		)
		(= temp0 (PicNotValid))
		(PicNotValid 0)
		(|= state $0020)
		(= temp1
			(self
				drawInvWindow: (if argc param1 else gEgo) (gIconBar curIcon?)
			)
		)
		(if (not temp1) (&= state $ffdf))
		(PicNotValid temp0)
		(return temp1)
	)
	
	(method (hide &tmp temp0)
		(if (& state $0020)
			(gSounds pause: 0)
			(&= state $ffdf)
		)
		(if window (window dispose:))
		(if
			(and
				(IsObject curIcon)
				(curIcon isKindOf: InventoryItem)
			)
			(if (not (gIconBar curInvIcon?))
				(gIconBar enable: (gIconBar useIconItem?))
			)
			(gIconBar
				curIcon: ((gIconBar useIconItem?)
					cursor: (curIcon cursor?)
					yourself:
				)
				curInvIcon: curIcon
			)
			(= temp0 ((gIconBar curIcon?) cursor?))
			(if temp0
				(gGame
					setCursor: (= temp0 ((gIconBar curIcon?) cursor?))
				)
			)
		)
	)
	
	(method (advance param1 &tmp temp0 temp1 temp2 temp3)
		(= temp1 (if argc param1 else 1))
		(= temp2 (self indexOf: highlightedIcon))
		(= temp3 (+ temp1 temp2))
		(repeat
			(= temp0
				(self
					at: (if (<= temp3 size) temp3 else (mod temp3 (- size 1)))
				)
			)
			(if (not (IsObject temp0))
				(= temp0 (NodeValue (self first:)))
			)
			(if (not (& (temp0 signal?) $0004)) (break))
			(++ temp3)
		)
		(self highlight: temp0 1)
	)
	
	(method (retreat param1 &tmp temp0 temp1 temp2 temp3)
		(= temp1 (if argc param1 else 1))
		(= temp2 (self indexOf: highlightedIcon))
		(= temp3 (- temp2 temp1))
		(repeat
			(= temp0 (self at: temp3))
			(if (not (IsObject temp0))
				(= temp0 (NodeValue (self last:)))
			)
			(if (not (& (temp0 signal?) $0004)) (break))
			(-- temp3)
		)
		(self highlight: temp0 1)
	)
	
	(method (ownedBy param1)
		(self firstTrue: 336 param1)
	)
	
	(method (drawInvWindow param1 param2 &tmp temp0 temp1 temp2 temp3 temp4 temp5 temp6 temp7 inventoryBaseFirst temp9 temp10 temp11 temp12 temp13 temp14 temp15 temp16 temp17 temp18 temp19 temp20 inventoryBaseWindow [temp22 50])
		(= temp5 0)
		(= temp4 temp5)
		(= temp3 temp4)
		(= temp2 temp3)
		(= temp1 temp2)
		(= temp0 temp1)
		(= inventoryBaseFirst (self first:))
		(while inventoryBaseFirst
			(= temp9 (NodeValue inventoryBaseFirst))
			(if (temp9 isKindOf: InventoryItem)
				(if (temp9 ownedBy: param1)
					(temp9 signal: (& (temp9 signal?) (~ $0004)))
					(++ temp0)
					(= temp6
						(CelWide (temp9 view?) (temp9 loop?) (temp9 cel?))
					)
					(if (> temp6 temp2) (= temp2 temp6))
					(= temp7
						(CelHigh (temp9 view?) (temp9 loop?) (temp9 cel?))
					)
					(if (> temp7 temp1) (= temp1 temp7))
				else
					(temp9 signal: (| (temp9 signal?) $0004))
				)
			else
				(++ temp3)
				(+= temp5
					(CelWide (temp9 view?) (temp9 loop?) (temp9 cel?))
				)
				(= temp7
					(CelHigh (temp9 view?) (temp9 loop?) (temp9 cel?))
				)
				(if (> temp7 temp4) (= temp4 temp7))
			)
			(= inventoryBaseFirst (self next: inventoryBaseFirst))
		)
		(if (not temp0)
			(Print addTextF: {%s %s} normalHeading empty init:)
			(return 0)
		)
		(= temp16 (Sqrt temp0))
		(if (> (* temp16 (= temp16 (Sqrt temp0))) temp0)
			(-- temp16)
		)
		(if (> temp16 3) (= temp16 3))
		(= local0 (/ temp0 temp16))
		(if (< (* temp16 local0) temp0) (++ local0))
		(= temp10 (Max (+ 4 temp5) (* local0 (+ 4 temp2))))
		(= temp11 (* temp16 (+ 4 temp1)))
		(= temp12 (/ (- 190 temp11) 2))
		(= temp13 (/ (- 320 temp10) 2))
		(= temp14 (+ temp12 temp11))
		(= temp15 (+ temp13 temp10))
		(= inventoryBaseWindow (self window?))
		(if inventoryBaseWindow
			((= inventoryBaseWindow (self window?))
				top: temp12
				left: temp13
				right: temp15
				bottom: temp14
				open:
			)
		)
		(= temp20 local0)
		(if temp0
			(= temp18
				(+
					2
					(if (inventoryBaseWindow respondsTo: 367)
						(inventoryBaseWindow yOffset?)
					else
						0
					)
				)
			)
			(= temp17
				(+
					4
					(if (inventoryBaseWindow respondsTo: 366)
						(inventoryBaseWindow xOffset?)
					else
						0
					)
				)
			)
			(= temp19 temp17)
			(= inventoryBaseFirst (self first:))
			(while inventoryBaseFirst
				(= temp9 (NodeValue inventoryBaseFirst))
				(if
					(and
						(not (& (temp9 signal?) $0004))
						(temp9 isKindOf: InventoryItem)
					)
					(if (not (& (temp9 signal?) $0080))
						(= temp6
							(CelWide (temp9 view?) (temp9 loop?) (temp9 cel?))
						)
						(= temp7
							(CelHigh (temp9 view?) (temp9 loop?) (temp9 cel?))
						)
						(temp9
							nsLeft: (+ temp17 (/ (- temp2 temp7) 2))
							nsTop: (+ temp18 (/ (- temp1 temp7) 2))
						)
						(temp9
							nsRight: (+ (temp9 nsLeft?) temp6)
							nsBottom: (+ (temp9 nsTop?) temp7)
						)
						(if (-- temp20)
							(+= temp17 temp2)
						else
							(= temp20 local0)
							(+= temp18 temp1)
							(= temp17 temp19)
						)
					else
						(= temp17 (temp9 nsLeft?))
						(= temp18 (temp9 nsTop?))
					)
					(temp9 show:)
					(if (== temp9 param2) (temp9 highlight:))
				)
				(= inventoryBaseFirst (self next: inventoryBaseFirst))
			)
		)
		(= temp17
			(/
				(-
					(-
						(inventoryBaseWindow right?)
						(inventoryBaseWindow left?)
					)
					temp5
				)
				2
			)
		)
		(= temp11
			(-
				(inventoryBaseWindow bottom?)
				(inventoryBaseWindow top?)
			)
		)
		(= temp18 32767)
		(= inventoryBaseFirst (self first:))
		(while inventoryBaseFirst
			(= temp9 (NodeValue inventoryBaseFirst))
			(if (not (temp9 isKindOf: InventoryItem))
				(temp9 nsTop: 0)
				(= temp6
					(CelWide (temp9 view?) (temp9 loop?) (temp9 cel?))
				)
				(= temp7
					(CelHigh (temp9 view?) (temp9 loop?) (temp9 cel?))
				)
				(if (not (& (temp9 signal?) $0080))
					(if (== temp18 32767) (= temp18 (- temp11 temp7)))
					(temp9
						nsLeft: temp17
						nsTop: temp18
						nsBottom: temp11
						nsRight: (+ temp17 temp6)
					)
				)
				(= temp17 (+ (temp9 nsLeft?) temp6))
				(= temp18 (temp9 nsTop?))
				(temp9 signal: (& (temp9 signal?) (~ $0004)) show:)
			)
			(= inventoryBaseFirst (self next: inventoryBaseFirst))
		)
		(return 1)
	)
)

(class ScrollableInventory of InventoryBase
	(properties
		elements 0
		size 0
		height 0
		underBits 0
		oldMouseX 0
		oldMouseY 0
		curIcon 0
		highlightedIcon 0
		prevIcon 0
		curInvIcon 0
		useIconItem 0
		helpIconItem 0
		walkIconItem 0
		port 0
		window 0
		state 1024
		activateHeight 0
		y 0
		normalHeading -1
		heading 0
		empty -1
		iconBarInvItem 0
		okButton 0
		selectIcon 0
		curPos 0
		dispAmount 12
		items 0
		numCols 6
		numRows 2
		scrollAmount 6
		firstThru 1
		upIcon 0
		downIcon 0
	)
	
	(method (dispose)
		(if (IsObject items) (items dispose:) (= items 0))
		(super dispose: &rest)
	)
	
	(method (hide)
		(if (IsObject items) (items dispose:) (= items 0))
		(= firstThru 1)
		(super hide: &rest)
	)
	
	(method (advance param1 &tmp temp0 temp1 temp2 temp3)
		(= temp1 (if argc param1 else 1))
		(= temp2 (self indexOf: highlightedIcon))
		(= temp3 (mod (+ temp1 temp2) size))
		(repeat
			(= temp0 (self at: temp3))
			(if
				(and
					(IsObject temp0)
					(not (& (temp0 signal?) $0004))
					(or
						(> (temp0 nsLeft?) -1)
						(not (temp0 isKindOf: InventoryItem))
					)
				)
				(break)
			)
			(= temp3 (mod (+ temp3 1) size))
		)
		(self highlight: temp0 1)
	)
	
	(method (retreat param1 &tmp temp0 temp1 temp2 temp3)
		(= temp1 (if argc param1 else 1))
		(if
			(<
				(= temp3
					(- (= temp2 (self indexOf: highlightedIcon)) temp1)
				)
				0
			)
			(= temp3 (- size 1))
		)
		(repeat
			(= temp0 (self at: temp3))
			(if
				(and
					(IsObject temp0)
					(not (& (temp0 signal?) $0004))
					(or
						(> (temp0 nsLeft?) -1)
						(not (temp0 isKindOf: InventoryItem))
					)
				)
				(break)
			)
			(if (< (-- temp3) 0) (= temp3 (- size 1)))
		)
		(self highlight: temp0 1)
	)
	
	(method (drawInvWindow param1 param2 &tmp theTheCurPos theTheTheCurPos theTheTheTheCurPos theTheTheTheTheCurPos theTheTheTheTheCurPos_2_2 theTheTheTheTheTheCurPos_2_2 theTheTheTheTheCurPos_2 theTheTheTheCurPos_2 scrollableInventoryFirst temp9 temp10 temp11 temp12 temp13 temp14 temp15 theTheTheTheTheCurPos_3 theTheTheTheCurPos_3 theTheTheTheTheTheCurPos_3 theNumCols theCurPos scrollableInventoryWindow)
		(= theTheTheTheTheTheCurPos_2_2 0)
		(= theTheTheTheTheCurPos_2_2 theTheTheTheTheTheCurPos_2_2)
		(= theTheTheTheTheCurPos theTheTheTheTheCurPos_2_2)
		(= theTheTheTheCurPos theTheTheTheTheCurPos)
		(= theTheTheCurPos theTheTheTheCurPos)
		(= theTheCurPos theTheTheCurPos)
		(= theCurPos theTheCurPos)
		(if firstThru
			(if (IsObject items) (items dispose:) (= items 0))
			(= items (Set new:))
		)
		(= scrollableInventoryFirst (self first:))
		(while scrollableInventoryFirst
			(= temp9 (NodeValue scrollableInventoryFirst))
			(if (temp9 isKindOf: InventoryItem)
				(if (temp9 ownedBy: param1)
					(temp9 signal: (& (temp9 signal?) $fffb))
					(items add: temp9)
					(temp9 nsLeft: -5 nsRight: -5 nsTop: -5 nsBottom: -5)
					(= theTheTheTheTheCurPos_2
						(CelWide (temp9 view?) (temp9 loop?) (temp9 cel?))
					)
					(if (> theTheTheTheTheCurPos_2 theTheTheTheCurPos)
						(= theTheTheTheCurPos theTheTheTheTheCurPos_2)
					)
					(= theTheTheTheCurPos_2
						(CelHigh (temp9 view?) (temp9 loop?) (temp9 cel?))
					)
					(if (> theTheTheTheCurPos_2 theTheTheCurPos)
						(= theTheTheCurPos theTheTheTheCurPos_2)
					)
				else
					(temp9 signal: (| (temp9 signal?) $0004))
				)
			else
				(++ theTheTheTheTheCurPos)
				(+= theTheTheTheTheTheCurPos_2_2
					(CelWide (temp9 view?) (temp9 loop?) (temp9 cel?))
				)
				(= theTheTheTheCurPos_2
					(CelHigh (temp9 view?) (temp9 loop?) (temp9 cel?))
				)
				(if
				(> theTheTheTheCurPos_2 theTheTheTheTheCurPos_2_2)
					(= theTheTheTheTheCurPos_2_2 theTheTheTheCurPos_2)
				)
			)
			(= scrollableInventoryFirst
				(self next: scrollableInventoryFirst)
			)
		)
		(if (not (items size?))
			(if (and (<= 0 normalHeading) (<= 0 empty))
				(Print addText: empty 0 0 0 0 0 normalHeading init:)
			else
				(Prints {You'll get nothing and like it!})
			)
			(if (IsObject items) (items dispose:))
			(return 0)
		)
		(= theTheCurPos
			(if (< (items size?) dispAmount)
				(items size?)
			else
				dispAmount
			)
		)
		(= temp10
			(Max
				(+ 4 theTheTheTheTheTheCurPos_2_2)
				(* numCols (+ 4 theTheTheTheCurPos))
			)
		)
		(= temp11 (* numRows (+ 4 theTheTheCurPos)))
		(= temp12 (/ (- 190 temp11) 2))
		(= temp13 (/ (- 320 temp10) 2))
		(= temp14 (+ temp12 temp11))
		(= temp15 (+ temp13 temp10))
		(= scrollableInventoryWindow (self window?))
		(if scrollableInventoryWindow
			(scrollableInventoryWindow
				top: temp12
				left: temp13
				right: temp15
				bottom: temp14
				open: (not firstThru)
			)
		)
		(= theNumCols numCols)
		(if theTheCurPos
			(= theTheTheTheCurPos_3
				(+
					2
					(if (scrollableInventoryWindow respondsTo: 367)
						(scrollableInventoryWindow yOffset?)
					else
						0
					)
				)
			)
			(= theTheTheTheTheCurPos_3
				(+
					4
					(if (scrollableInventoryWindow respondsTo: 366)
						(scrollableInventoryWindow xOffset?)
					else
						0
					)
				)
			)
			(= theTheTheTheTheTheCurPos_3 theTheTheTheTheCurPos_3)
			(= theCurPos curPos)
			(while
				(and
					(< theCurPos (+ curPos dispAmount))
					(< theCurPos (items size?))
				)
				(= temp9 (items at: theCurPos))
				(if (not (& (temp9 signal?) $0080))
					(= theTheTheTheTheCurPos_2
						(CelWide (temp9 view?) (temp9 loop?) (temp9 cel?))
					)
					(= theTheTheTheCurPos_2
						(CelHigh (temp9 view?) (temp9 loop?) (temp9 cel?))
					)
					(temp9
						nsLeft:
							(+
								theTheTheTheTheCurPos_3
								(/ (- theTheTheTheCurPos theTheTheTheCurPos_2) 2)
							)
						nsTop:
							(+
								theTheTheTheCurPos_3
								(/ (- theTheTheCurPos theTheTheTheCurPos_2) 2)
							)
					)
					(temp9
						nsRight: (+ (temp9 nsLeft?) theTheTheTheTheCurPos_2)
						nsBottom: (+ (temp9 nsTop?) theTheTheTheCurPos_2)
					)
					(if (-- theNumCols)
						(+= theTheTheTheTheCurPos_3 theTheTheTheCurPos)
					else
						(= theNumCols numCols)
						(+= theTheTheTheCurPos_3 theTheTheCurPos)
						(= theTheTheTheTheCurPos_3 theTheTheTheTheTheCurPos_3)
					)
				else
					(= theTheTheTheTheCurPos_3 (temp9 nsLeft?))
					(= theTheTheTheCurPos_3 (temp9 nsTop?))
				)
				(temp9 show:)
				(if (== temp9 param2) (temp9 highlight:))
				(++ theCurPos)
			)
		)
		(= theTheTheTheTheCurPos_3
			(/
				(-
					(-
						(scrollableInventoryWindow right?)
						(scrollableInventoryWindow left?)
					)
					theTheTheTheTheTheCurPos_2_2
				)
				2
			)
		)
		(= temp11
			(-
				(scrollableInventoryWindow bottom?)
				(scrollableInventoryWindow top?)
			)
		)
		(= theTheTheTheCurPos_3 32767)
		(if firstThru
			(= scrollableInventoryFirst (self first:))
			(while scrollableInventoryFirst
				(= temp9 (NodeValue scrollableInventoryFirst))
				(if (not (temp9 isKindOf: InventoryItem))
					(= theTheTheTheTheCurPos_2
						(CelWide (temp9 view?) (temp9 loop?) (temp9 cel?))
					)
					(= theTheTheTheCurPos_2
						(CelHigh (temp9 view?) (temp9 loop?) (temp9 cel?))
					)
					(if (not (& (temp9 signal?) $0080))
						(if (== theTheTheTheCurPos_3 32767)
							(= theTheTheTheCurPos_3 (- temp11 theTheTheTheCurPos_2))
						)
						(temp9
							nsLeft: theTheTheTheTheCurPos_3
							nsTop: theTheTheTheCurPos_3
							nsBottom: (+ theTheTheTheCurPos_3 theTheTheTheCurPos_2)
							nsRight: (+ theTheTheTheTheCurPos_3 theTheTheTheTheCurPos_2)
						)
					)
					(= theTheTheTheTheCurPos_3
						(+ (temp9 nsLeft?) theTheTheTheTheCurPos_2)
					)
					(= theTheTheTheCurPos_3 (temp9 nsTop?))
					(temp9 signal: (& (temp9 signal?) $fffb))
					(temp9 show:)
				)
				(= scrollableInventoryFirst
					(self next: scrollableInventoryFirst)
				)
			)
		)
		(if (not curPos)
			(upIcon signal: (| (upIcon signal?) $0004))
		else
			(upIcon signal: (& (upIcon signal?) (~ $0004)))
		)
		(if (>= curPos (- (items size?) dispAmount))
			(downIcon signal: (| (downIcon signal?) $0004))
		else
			(downIcon signal: (& (downIcon signal?) (~ $0004)))
		)
		(upIcon show:)
		(downIcon show:)
		(return 1)
	)
	
	(method (scroll param1)
		(cond 
			((and argc (> 0 param1)) (if (< (-= curPos scrollAmount) 0) (= curPos 0)))
			(
			(> (+= curPos scrollAmount) (- size dispAmount)) (= curPos (- size dispAmount)))
		)
		(= firstThru 0)
		(selectIcon select:)
		(self show: gEgo)
	)
)
