;;; Sierra Script 1.0 - (do not remove this comment)
(script# 947)
(include sci.sh)
(use Main)
(use Controls)
(use Print)
(use PolygonEdit)
(use SysWindow)
(use File)
(use System)


(local
	theWindow
	local1
	[local2 40]
	[local42 20]
	[local62 28] = [{DIALOG} 0 0 {About} 0 0 {Item} 0 0 {Edit} 0 0 {Look} 0 0 {Del} 0 0 {Win} 0 0 {Help} 0 0 {eXit} 120]
	[local90 22] = [{EDITING} 0 0 {Text} 0 0 {Font} 0 0 {Value} 0 0 {Position} 0 0 {Help} 0 0 {Menu}]
	[local112 25] = [{EDITING} 0 0 {Text} 0 0 {Font} 0 0 {Just} 0 0 {Width} 0 0 {Position} 0 0 {Help} 0 0 {Menu}]
	[local137 22] = [{EDITING} 0 0 {Font} 0 0 {Length} 0 0 {Position} 0 0 {Text} 0 0 {Help} 0 0 {Menu}]
	[local159 22] = [{EDITING} 0 0 {View} 0 0 {Loop} 0 0 {Cel} 0 0 {Position} 0 0 {Help} 0 0 {Menu}]
	[local181 19] = [{EDITING} 0 0 {Width} 0 0 {Length} 0 0 {Position} 0 0 {Help} 0 0 {Menu}]
	[local200 22] = [{WINDOW} 0 0 {Create} 0 0 {Position} 0 0 {Delete} 0 0 {Title} 0 0 {Help} 0 0 {Menu}]
)
(procedure (localproc_027c param1 param2)
	(if (> (StrLen param1) param2)
		(StrAt param1 param2 0)
		(StrAt param1 (-- param2) 46)
		(StrAt param1 (-- param2) 46)
		(StrAt param1 (-- param2) 46)
	)
	(return param1)
)

(procedure (localproc_02b6 param1 &tmp temp0 temp1 temp2 temp3 temp4 [temp5 10] [temp15 10] [temp25 10] [temp35 10] [temp45 12])
	(= temp0 (param1 noun?))
	(= temp1 (param1 verb?))
	(= temp2 (param1 case?))
	(= temp3 (param1 seq?))
	(= temp4 (param1 modNum?))
	(Format @temp5 {%d} temp0)
	(Format @temp15 {%d} temp1)
	(Format @temp25 {%d} temp2)
	(Format @temp35 {%d} temp3)
	(Format @temp45 {%d} temp4)
	(return
		(if
			(Print
				addTitle: @local42
				font: 0
				addText: {Enter new message parameters:}
				addText: {Noun} 5 25
				addText: {Verb} 85 25
				addText: {Case} 5 39
				addText: {Seq} 85 39
				addText: {Module} 47 53
				addEdit: @temp5 4 45 25 @temp5
				addEdit: @temp15 4 125 25 @temp15
				addEdit: @temp25 4 45 39 @temp25
				addEdit: @temp35 4 125 39 @temp35
				addEdit: @temp45 5 101 53 @temp45
				addButton: 1 {___OK___} 18 67
				addButton: 0 {Cancel} 91 67
				init:
			)
			(= temp0 (ReadNumber @temp5))
			(= temp1 (ReadNumber @temp15))
			(= temp2 (ReadNumber @temp25))
			(= temp3 (ReadNumber @temp35))
			(= temp4 (ReadNumber @temp45))
			(cond 
				((not (Message 0 temp4 temp0 temp1 temp2 temp3)) (Prints {Can't find message!}) (return 0))
				((not (Message 2 temp4 temp0 temp1 temp2 temp3)) (Prints {Message contains no text!}) (return 0))
				(else
					(param1
						noun: temp0
						verb: temp1
						case: temp2
						seq: temp3
						modNum: temp4
					)
					(return 1)
				)
			)
		else
			(return 0)
		)
	)
)

(procedure (localproc_04a3 param1)
	(Print
		addTitle: @local42
		font: 0
		width: 50
		addText: {What kind of text?}
		addButton: 1 { Literal_} 60 0
		addButton: 2 {MSG file} 60 14
		addButton: 0 {__Cancel__} 60 28
		first: (if (param1 seq?) 2 else 1)
		init:
	)
)

(class DlgWindow of SysWindow
	(properties
		top 0
		left 0
		bottom 0
		right 0
		color 0
		back 15
		priority 15
		window 0
		type 0
		title 0
		brTop 0
		brLeft 0
		brBottom 190
		brRight 320
		lsTop 0
		lsLeft 0
		lsBottom 0
		lsRight 0
		eraseOnly 0
		noun 0
		verb 0
		case 0
		seq 0
		modNum 0
	)
	
	(method (dispose)
		(super dispose:)
		(= local1 0)
	)
	
	(method (open &tmp temp0)
		(= type (if title 4 else 0))
		(= temp0 (GetPort))
		(super open: &rest)
		(SetPort temp0)
		(= local1 1)
	)
	
	(method (create &tmp temp0 temp1 temp2 temp3 temp4 temp5)
		(= temp0 190)
		(= temp1 320)
		(= temp2 0)
		(= temp3 0)
		(= temp4 0)
		(while (< temp4 (DialogEditor size?))
			(= temp5 (DialogEditor at: temp4))
			(= temp0 (Min (temp5 nsTop?) temp0))
			(= temp1 (Min (temp5 nsLeft?) temp1))
			(= temp2 (Max (temp5 nsBottom?) temp2))
			(= temp3 (Max (temp5 nsRight?) temp3))
			(++ temp4)
		)
		(DialogEditor eachElementDo: 102)
		(self dispose:)
		(= top (- temp0 4))
		(= bottom (+ temp2 4))
		(= left (- temp1 4))
		(= right (+ temp3 4))
		(self open:)
		(DialogEditor eachElementDo: 80)
	)
	
	(method (moveTo theLeft theTop &tmp temp0 temp1 temp2 temp3)
		(= temp0 0)
		(while (< temp0 (DialogEditor size?))
			(= temp1 (DialogEditor at: temp0))
			(= temp2 (- (temp1 nsLeft?) left))
			(= temp3 (- (temp1 nsTop?) top))
			(temp1 hide: moveTo: (+ theLeft temp2) (+ theTop temp3))
			(++ temp0)
		)
		(= left theLeft)
		(= top theTop)
		(self create:)
	)
	
	(method (editMsg)
		(if (localproc_02b6 self)
			(if title (Memory 3 title))
			(= title
				(Memory 1 (Message 2 modNum noun verb case seq))
			)
			(Message 0 modNum noun verb case seq title)
		)
		(self create:)
	)
	
	(method (editPosn &tmp newEvent newEventX temp2 temp3 temp4)
		(if local1
			(Prints
				{Click to where the top left of the window should be}
			)
			(while (!= ((= newEvent (Event new:)) type?) 1)
				(newEvent dispose:)
			)
			(= newEventX (newEvent x?))
			(= temp2 (- (newEvent y?) 10))
			(newEvent dispose:)
			(= temp3 (- bottom top))
			(= temp4 (- right left))
			(= newEventX (Max 0 (Min newEventX (- 320 temp4))))
			(= temp2 (Max 0 (Min temp2 (- 190 temp3))))
			(self moveTo: newEventX temp2)
		else
			(Prints {No window to position!})
		)
	)
	
	(method (editTitle)
		(switch (localproc_04a3 self)
			(0 (return))
			(2 (self editMsg:) (return))
		)
		(if seq
			(Memory 3 title)
			(= noun
				(= verb (= case (= seq (= modNum (= title 0)))))
			)
		)
		(if (not title)
			(= title (Memory 1 50))
			(StrCpy title {title})
		)
		(Print
			addTitle: @local42
			addText: {Enter new title:}
			addEdit: title 50 0 12 title
			init:
		)
		(self create:)
	)
)

(class _DItem of Control
	(properties
		type 0
		state 0
		nsTop 0
		nsLeft 0
		nsBottom 0
		nsRight 0
		key 0
		said 0
		value 0
		underBits 0
	)
	
	(method (dispose)
		(self hide:)
		(super dispose: &rest)
	)
	
	(method (select param1)
		(self hide:)
		(if param1 (|= state $0008) else (&= state (~ $0008)))
		(self draw:)
	)
	
	(method (handleEvent param1 &tmp temp0)
		(= temp0 0)
		(param1 claimed: 1)
		(= temp0 (self track: param1))
	)
	
	(method (track param1 &tmp temp0 temp1 [temp2 40])
		(if (== (param1 type?) 1)
			(self hide:)
			(repeat
				(= param1 (Event new: -32768))
				(param1 localize:)
				(= temp0 (param1 x?))
				(= temp1 (param1 y?))
				(DrawStatus
					(Format @temp2 {DRAGGING: %d, %d} temp0 temp1)
				)
				(self moveTo: temp0 temp1)
				(param1 dispose:)
				(breakif(not (MouseStillDown)))
			)
			(DrawStatus {_} 0 0)
			(DrawStatus 0)
			(if (DialogEditor curMenu?)
				((DialogEditor curMenu?) init:)
			)
			(DrawPic (gRoom picture?) 100)
			(if local1
				(DlgWindow create:)
			else
				(DialogEditor eachElementDo: 80)
			)
		)
	)
	
	(method (draw &tmp temp0 temp1 temp2 temp3)
		(= temp0 (- nsTop 1))
		(= temp1 (- nsLeft 1))
		(= temp2 (+ nsBottom 1))
		(= temp3 (+ nsRight 1))
		(if underBits (UnLoad 133 underBits) (= underBits 0))
		(= underBits (Graph 7 temp0 temp1 temp2 temp3 1))
		(DrawControl self)
	)
	
	(method (hide &tmp temp0 temp1 temp2 temp3)
		(if underBits
			(Graph 8 underBits)
			(= temp0 (- nsTop 1))
			(= temp1 (- nsLeft 1))
			(= temp2 (+ nsBottom 1))
			(= temp3 (+ nsRight 1))
			(= underBits 0)
			(Graph 13 temp0 temp1 temp2 temp3)
		)
	)
	
	(method (editPosn &tmp [temp0 25] [temp25 5] temp30 temp31 temp32)
		(Format @temp0 {%d} nsLeft)
		(Format @temp25 {%d} nsTop)
		(= temp30
			(Print
				addTitle: @local42
				addText: {Enter new position:}
				addText: {x =_} 0 12
				addText: {y =_} 65 12
				addEdit: @temp0 5 25 12 @temp0
				addEdit: @temp25 6 90 12 @temp25
				font: 0
				addButton: 0 { Cancel_} 35 26
				init:
			)
		)
		(if temp30
			(= temp31 (Max 4 (ReadNumber @temp0)))
			(= temp32 (Max 4 (ReadNumber @temp25)))
			(self hide: moveTo: temp31 temp32 draw:)
		)
	)
)

(class _DText of _DItem
	(properties
		type 2
		state 0
		nsTop 0
		nsLeft 0
		nsBottom 0
		nsRight 0
		key 0
		said 0
		value 0
		underBits 0
		text 0
		font 0
		mode 0
		width 0
		noun 0
		verb 0
		case 0
		seq 0
		modNum 0
	)
	
	(method (setSize param1 &tmp [temp0 2] temp2 temp3)
		(TextSize @temp0 text font (if argc param1 else width))
		(-- nsLeft)
		(-- nsTop)
		(= nsBottom (+ nsTop temp2 1))
		(= nsRight (+ nsLeft temp3 1))
	)
	
	(method (showHelp)
		(Print
			font: gFont
			width: 250
			addText:
				{Text Menu:\n\n__Text - Change the text\n__Font - Change the font of the text\n__Just - Change justification mode\n__Position - Change the position of the text\n__Menu - Return to the Main Menu\n}
			init:
		)
	)
	
	(method (editFont &tmp [temp0 25] temp25)
		(= temp25
			(Print
				addTitle: @local42
				font: 0
				width: 90
				addText: {Enter new font number:}
				addEdit: @temp0 6 0 24
				addButton: 0 { System_} 100 0
				addButton: gFont {__User__} 100 14
				addButton: gSmallFont {__Small__} 100 28
				addButton: gBigFont {___Big___} 100 42
				addButton: -1 { Cancel_} 100 56
				init:
			)
		)
		(if temp0 (= temp25 (ReadNumber @temp0)))
		(if (!= temp25 -1)
			(self hide: font: temp25 setSize: draw:)
		)
	)
	
	(method (editJust &tmp [temp0 25] temp25)
		(= temp0 0)
		(= temp25
			(Print
				addTitle: @local42
				font: 0
				width: 100
				addText: {Enter justification mode:}
				addButton: 1 {___Left___} 100 0
				addButton: 2 { Center_} 100 14
				addButton: 3 {__Right__} 100 28
				addButton: 0 { Cancel_} 100 42
				init:
			)
		)
		(if temp0 (= temp25 (ReadNumber @temp0)))
		(if temp25
			(self
				hide:
				mode:
				(switch temp25
					(1 0)
					(2 1)
					(3 -1)
				)
				setSize:
				draw:
			)
		)
	)
	
	(method (editMsg)
		(if (localproc_02b6 self)
			(Memory 3 text)
			(= text
				(Memory 1 (Message 2 modNum noun verb case seq))
			)
			(Message 0 modNum noun verb case seq text)
		)
		(self hide: setSize: draw:)
	)
	
	(method (editText)
		(switch (localproc_04a3 self)
			(0 (return))
			(2 (self editMsg:) (return))
		)
		(if seq
			(Memory 3 text)
			(= text (Memory 1 100))
			(StrCpy text {text})
			(= noun (= verb (= case (= seq (= modNum 0)))))
		)
		(Print
			addTitle: @local42
			addText: {Enter new text:}
			addEdit: text 50 0 12 text
			init:
		)
		(self hide: setSize: draw:)
	)
	
	(method (editWidth &tmp [temp0 25] temp25)
		(Format @temp0 {%d} width)
		(= temp25
			(Print
				addTitle: @local42
				addText: {Enter new width:}
				addEdit: @temp0 6 0 12 @temp0
				font: 0
				addButton: 0 { Cancel_} 0 26
				init:
			)
		)
		(if temp0 (= temp25 (ReadNumber @temp0)))
		(if temp25 (self hide: width: temp25 setSize: draw:))
	)
)

(class _DIcon of _DItem
	(properties
		type 4
		state 0
		nsTop 0
		nsLeft 0
		nsBottom 0
		nsRight 0
		key 0
		said 0
		value 0
		underBits 0
		view 0
		loop 0
		cel 0
	)
	
	(method (setSize)
		(= nsRight (+ nsLeft (CelWide view loop cel)))
		(= nsBottom (+ nsTop (CelHigh view loop cel)))
	)
	
	(method (showHelp)
		(Print
			font: gFont
			width: 250
			addText:
				{Icon Menu:\n\n__View - Change the view of the icon\n__Loop - Change the loop of the icon\n__Cel - Change the cel of the icon\n__Position - Change the position of the icon\n__Menu - Return to the Main Menu\n}
			init:
		)
	)
	
	(method (editView &tmp [temp0 25] theView)
		(Format @temp0 {%d} view)
		(= theView
			(Print
				addTitle: @local42
				addText: {Enter new view number:}
				addEdit: @temp0 5 0 12 @temp0
				font: 0
				addButton: 0 { Cancel_} 0 26
				init:
			)
		)
		(if temp0 (= theView (ReadNumber @temp0)))
		(if theView (= view theView))
		(self hide: setSize: draw:)
	)
	
	(method (editLoop &tmp [temp0 25] theLoop)
		(Format @temp0 {%d} loop)
		(= theLoop
			(Print
				addTitle: @local42
				addText: {Enter new loop number:}
				addEdit: @temp0 5 0 12 @temp0
				font: 0
				addButton: 0 { Cancel_} 0 26
				init:
			)
		)
		(if temp0 (= theLoop (ReadNumber @temp0)))
		(if theLoop (= loop theLoop))
		(self hide: setSize: draw:)
	)
	
	(method (editCel &tmp [temp0 25] theCel)
		(Format @temp0 {%d} cel)
		(= theCel
			(Print
				addTitle: @local42
				addText: {Enter new cel number:}
				addEdit: @temp0 5 0 12 @temp0
				font: 0
				addButton: 0 { Cancel_} 0 26
				init:
			)
		)
		(if temp0 (= theCel (ReadNumber @temp0)))
		(if theCel (= cel theCel))
		(self hide: setSize: draw:)
	)
)

(class _DButton of _DItem
	(properties
		type 1
		state 1
		nsTop 0
		nsLeft 0
		nsBottom 0
		nsRight 0
		key 0
		said 0
		value 0
		underBits 0
		text 0
		font 0
		noun 0
		verb 0
		case 0
		seq 0
		modNum 0
	)
	
	(method (setSize &tmp [temp0 2] temp2 temp3)
		(TextSize @temp0 text font 0 0)
		(+= temp2 2)
		(+= temp3 2)
		(= nsBottom (+ nsTop temp2))
		(= temp3 (* (/ (+ temp3 15) 16) 16))
		(= nsRight (+ temp3 nsLeft))
	)
	
	(method (showHelp)
		(Print
			font: gFont
			width: 250
			addText:
				{Button Menu:\n\n__Text - Change the button text\n__Font - Change the font of the button text\n__Value - Change the return value of the button\n__Position - Change the position of the button\n__Menu - Return to the Main Menu\n}
			init:
		)
	)
	
	(method (editFont &tmp [temp0 25] temp25)
		(= temp25
			(Print
				addTitle: @local42
				font: 0
				width: 90
				addText: {Enter new font number:}
				addEdit: @temp0 6 0 24
				addButton: 0 { System_} 100 0
				addButton: gFont {__User__} 100 14
				addButton: gSmallFont {__Small__} 100 28
				addButton: gBigFont {___Big___} 100 42
				addButton: -1 { Cancel_} 100 56
				init:
			)
		)
		(if temp0 (= temp25 (ReadNumber @temp0)))
		(if (!= temp25 -1)
			(self hide: font: temp25 setSize: draw:)
		)
	)
	
	(method (editMsg)
		(if (localproc_02b6 self)
			(Memory 3 text)
			(= text
				(Memory 1 (Message 2 modNum noun verb case seq))
			)
			(Message 0 modNum noun verb case seq text)
		)
		(self hide: setSize: draw:)
	)
	
	(method (editText)
		(switch (localproc_04a3 self)
			(0 (return))
			(2 (self editMsg:) (return))
		)
		(if seq
			(Memory 3 text)
			(= text (Memory 1 50))
			(StrCpy text {button})
			(= noun (= verb (= case (= seq (= modNum 0)))))
		)
		(Print
			addTitle: @local42
			addText: {Enter new text:}
			addEdit: text 50 0 12 text
			init:
		)
		(self hide: setSize: draw:)
	)
	
	(method (editValue &tmp [temp0 25] theValue)
		(Format @temp0 {%d} value)
		(= theValue
			(Print
				addTitle: @local42
				addText: {Enter new value:}
				addEdit: @temp0 6 0 12 @temp0
				font: 0
				addButton: 0 { Cancel_} 0 26
				init:
			)
		)
		(if temp0 (= theValue (ReadNumber @temp0)))
		(if theValue (= value theValue))
	)
)

(class _DEdit of _DItem
	(properties
		type 3
		state 1
		nsTop 0
		nsLeft 0
		nsBottom 0
		nsRight 0
		key 0
		said 0
		value 0
		underBits 0
		text 0
		font 0
		max 0
		cursor 0
	)
	
	(method (setSize &tmp [temp0 2] temp2 temp3)
		(TextSize @temp0 {M} font 0 0)
		(= nsBottom (+ nsTop temp2))
		(= nsRight (+ nsLeft (/ (* temp3 max 3) 4)))
		(= cursor (StrLen text))
	)
	
	(method (showHelp)
		(Print
			font: gFont
			width: 250
			addText:
				{Edit Menu:\n\n__Font - Change the font of the edit text\n__Length - Change the maximum length of input\n__Position - Change the position of the edit\n__Text - Change the default edit text\n__Menu - Return to the Main Menu\n}
			init:
		)
	)
	
	(method (editFont &tmp [temp0 25] temp25)
		(= temp25
			(Print
				addTitle: @local42
				font: 0
				width: 90
				addText: {Enter new font number:}
				addEdit: @temp0 6 0 24
				addButton: 0 { System_} 100 0
				addButton: gFont {__User__} 100 14
				addButton: gSmallFont {__Small__} 100 28
				addButton: gBigFont {___Big___} 100 42
				addButton: -1 { Cancel_} 100 56
				init:
			)
		)
		(if temp0 (= temp25 (ReadNumber @temp0)))
		(if (!= temp25 -1)
			(self hide: font: temp25 setSize: draw:)
		)
	)
	
	(method (editLength &tmp [temp0 25] temp25)
		(Format @temp0 {%d} max)
		(= temp25
			(Print
				addTitle: @local42
				addText: {Enter new maximum length:}
				addEdit: @temp0 5 0 12 @temp0
				font: 0
				addButton: 0 { Cancel_} 0 26
				init:
			)
		)
		(if temp0 (= temp25 (ReadNumber @temp0)))
		(if temp25 (self max: temp25 hide: setSize: draw:))
	)
	
	(method (editText)
		(Print
			addTitle: @local42
			addText: {Enter new default text:}
			addEdit: text 25 0 12 text
			init:
		)
		(self hide: setSize: draw:)
	)
)

(class _DSelector of _DItem
	(properties
		type 6
		state 0
		nsTop 0
		nsLeft 0
		nsBottom 0
		nsRight 0
		key 0
		said 0
		value 0
		underBits 0
		font 0
		x 20
		y 6
	)
	
	(method (setSize &tmp [temp0 2] temp2 temp3)
		(TextSize @temp0 {M} font 0 0)
		(= nsBottom (+ nsTop 20 (* temp2 y)))
		(= nsRight (+ nsLeft (/ (* temp3 x 3) 4)))
	)
	
	(method (showHelp)
		(Print
			font: gFont
			width: 250
			addText:
				{Selector Menu:\n\n__Width - Change the selector width (in chars)\n__Length - Change number of selector lines\n__Position - Change the position of the selector\n__Menu - Return to the Main Menu\n}
			init:
		)
	)
	
	(method (editLength &tmp [temp0 25] temp25)
		(Format @temp0 {%d} y)
		(= temp25
			(Print
				addTitle: @local42
				addText: {Enter new length:}
				addEdit: @temp0 5 0 12 @temp0
				font: 0
				addButton: 0 { Cancel_} 0 26
				init:
			)
		)
		(if temp0 (= temp25 (ReadNumber @temp0)))
		(if temp25 (self y: temp25 hide: setSize: draw:))
	)
	
	(method (editWidth &tmp [temp0 25] temp25)
		(Format @temp0 {%d} x)
		(= temp25
			(Print
				addTitle: @local42
				addText: {Enter new width:}
				addEdit: @temp0 5 0 12 @temp0
				font: 0
				addButton: 0 { Cancel_} 0 26
				init:
			)
		)
		(if temp0 (= temp25 (ReadNumber @temp0)))
		(if temp25 (self x: temp25 hide: setSize: draw:))
	)
)

(class DialogEditor of List
	(properties
		elements 0
		size 0
		state 0
		curItem 0
		curMenu 0
	)
	
	(method (init)
		(= theWindow gWindow)
		(StrCpy @local42 {DialogEditor__v1.1})
		(= gWindow SysWindow)
		(gWindow color: gColorWindowForeground back: 255)
		(gGame setCursor: 999)
		(self changeState: 0)
	)
	
	(method (doit &tmp newEvent [temp1 100])
		(self init:)
		(repeat
			(= newEvent (Event new:))
			(if (not (curMenu handleEvent: newEvent))
				(GlobalToLocal newEvent)
				(if (self handleEvent: newEvent) (break))
			)
			(newEvent dispose:)
		)
		(newEvent dispose:)
		(self dispose:)
	)
	
	(method (dispose)
		(mainMenu dispose:)
		(DlgWindow dispose:)
		(= gWindow theWindow)
		(gGame setCursor: ((gIconBar curIcon?) cursor?))
		(DrawStatus {_} 0 0)
		(DrawStatus 0)
		(super dispose:)
		(DrawPic (gRoom picture?) 100)
		(DisposeScript 947)
	)
	
	(method (handleEvent param1 &tmp temp0 theCurItem [temp2 506] [temp508 30] [temp538 30] [temp568 30] [temp598 30] [temp628 30] [temp658 20] [temp678 30] [temp708 30] [temp738 20] [temp758 30] [temp788 30])
		(switch (param1 type?)
			(1
				(= theCurItem (self firstTrue: 180 param1))
				(if theCurItem
					(param1 claimed: 1)
					(if (== (param1 modifiers?) 3)
						(theCurItem handleEvent: param1)
					else
						(if curItem (curItem select: 0))
						((= curItem theCurItem) select: 1)
						(self changeState: 0)
					)
				)
			)
			(4
				(switch (param1 message?)
					(63 (param1 message: 104))
					(19 (param1 message: 120))
				)
				(switch (param1 message?)
					(9
						(curItem select: 0)
						(= temp0 (DialogEditor indexOf: curItem))
						(if (== (++ temp0) (DialogEditor size?))
							(= curItem (DialogEditor at: 0))
						else
							(= curItem (DialogEditor at: temp0))
						)
						(curItem select: 1)
						(DialogEditor changeState: 0)
					)
					(3840
						(curItem select: 0)
						(= temp0 (DialogEditor indexOf: curItem))
						(if (== temp0 0)
							(= curItem
								(DialogEditor at: (- (DialogEditor size?) 1))
							)
						else
							(= curItem (DialogEditor at: (-- temp0)))
						)
					)
					(97
						(Print
							addTitle: @local42
							mode: 1
							width: 140
							addText: {by Brian K. Hughes\n17 July, 1992}
							init:
						)
					)
					(99
						(switch state
							(4 (curItem editCel:))
							(6
								(if size
									(DlgWindow create:)
								else
									(Prints {Can't create window: no items!})
								)
							)
						)
					)
					(100
						(switch state
							(0 (self delItem:))
							(6
								(if local1
									(DlgWindow dispose:)
									(self eachElementDo: 80)
								else
									(Prints {No window to delete!})
								)
							)
						)
					)
					(101
						(if size
							(self
								changeState:
									(switch (curItem -super-?)
										(_DButton 1)
										(_DText 2)
										(_DEdit 3)
										(_DIcon 4)
										(_DSelector 5)
									)
							)
						else
							(Prints {No items to edit!})
						)
					)
					(102
						(if (IsOneOf state 2 1 3) (curItem editFont:))
					)
					(104
						(switch state
							(0
								(Print
									font: gFont
									width: 250
									addText:
										{Main Menu:\n\n__About - About the DialogEditor\n__Item - Create a new item\n__Edit - Edit properties of the current item\n__Look - View properties of the current item\n__Del - Delete the current item\n__Win - Create a window background\n__Help - You're here!\n__eXit - Exit the DialogEditor (& maybe save)\n}
									init:
								)
							)
							(6
								(Print
									font: gFont
									width: 250
									addText:
										{Window Menu:\n\n__Create - Draw the window to the correct size\n__Position - Move the window and all items\n__Delete - Remove the window\n__Menu - Return to the Main Menu}
									init:
								)
							)
							(else  (curItem showHelp:))
						)
					)
					(105
						(= theCurItem 0)
						(switch
							(Print
								addTitle: @local42
								width: 65
								addText: {Select the kind of item you want to add to the dialog:}
								font: 0
								addButton: 1 {__Button__} 80 0
								addButton: 2 {___Text___} 80 14
								addButton: 3 {___Edit___} 80 28
								addButton: 4 {___Icon___} 80 42
								addButton: 6 { Selector_} 80 56
								addButton: 0 {__Cancel__} 80 70
								init:
							)
							(1
								(= temp0 (Memory 1 50))
								(StrCpy temp0 {button})
								(= theCurItem ((_DButton new:) text: temp0 yourself:))
							)
							(2
								(= temp0 (Memory 1 100))
								(StrCpy temp0 {text})
								(= theCurItem ((_DText new:) text: temp0 yourself:))
							)
							(3
								(= temp0 (Memory 1 50))
								(StrCpy temp0 {})
								(= theCurItem
									((_DEdit new:) text: temp0 max: 5 yourself:)
								)
							)
							(4
								(= theCurItem
									((_DIcon new:) view: 0 loop: 0 cel: 0 yourself:)
								)
							)
							(6
								(= theCurItem ((_DSelector new:) x: 10 y: 1 yourself:))
							)
						)
						(if theCurItem
							(if curItem (curItem select: 0))
							(= curItem theCurItem)
							(if local1 (DlgWindow dispose:))
							(self
								addToEnd: (curItem setSize: moveTo: 4 4 yourself:)
								eachElementDo: 80
							)
							(curItem select: 1)
						)
					)
					(106
						(if (== state 2) (curItem editJust:))
					)
					(108
						(switch state
							(0
								(if curItem
									(Format @temp508 {__x:_______%d} (curItem nsLeft?))
									(Format @temp538 {__y:_______%d} (curItem nsTop?))
									(switch (curItem -super-?)
										(_DText
											(StrCpy @temp788 {Text Item})
											(StrCpy @temp2 (curItem text?))
											(Format
												@temp568
												{__text:___%s}
												(localproc_027c @temp2 15)
											)
											(Format @temp598 {__font:___%d} (curItem font?))
											(Format @temp628 {__width:__%d} (curItem width?))
											(Format @temp658 {__noun:___%d} (curItem noun?))
											(Format @temp678 {__verb:___%d} (curItem verb?))
											(Format @temp708 {__case:___%d} (curItem case?))
											(Format @temp738 {__seq:____%d} (curItem seq?))
											(Format @temp758 {__modNum: %d} (curItem modNum?))
										)
										(_DButton
											(StrCpy @temp788 {Button Item})
											(StrCpy @temp2 (curItem text?))
											(Format
												@temp568
												{__text:___%s}
												(localproc_027c @temp2 15)
											)
											(Format @temp598 {__font:___%d} (curItem font?))
											(Format @temp628 {__value:__%d} (curItem value?))
											(Format @temp658 {__noun:___%d} (curItem noun?))
											(Format @temp678 {__verb:___%d} (curItem verb?))
											(Format @temp708 {__case:___%d} (curItem case?))
											(Format @temp738 {__seq:____%d} (curItem seq?))
											(Format @temp758 {__modNum: %d} (curItem modNum?))
										)
										(_DIcon
											(StrCpy @temp788 {Icon Item})
											(Format @temp568 {__view:___%d} (curItem view?))
											(Format @temp598 {__loop:___%d} (curItem loop?))
											(Format @temp628 {__cel:____%d} (curItem cel?))
											(= temp758 0)
											(= temp738 temp758)
											(= temp708 temp738)
											(= temp678 temp708)
											(= temp658 temp678)
										)
										(_DEdit
											(StrCpy @temp788 {Edit Item})
											(StrCpy @temp2 (curItem text?))
											(Format
												@temp568
												{__text:___%s}
												(localproc_027c @temp2 15)
											)
											(Format @temp598 {__font:___%d} (curItem font?))
											(Format @temp628 {__max:____%d} (curItem max?))
											(= temp758 0)
											(= temp738 temp758)
											(= temp708 temp738)
											(= temp678 temp708)
											(= temp658 temp678)
										)
										(_DSelector
											(StrCpy @temp788 {Selector Item})
											(Format @temp568 {__width:__%d} (curItem x?))
											(Format @temp598 {__length: %d} (curItem y?))
											(= temp758 0)
											(= temp738 temp758)
											(= temp708 temp738)
											(= temp678 temp708)
											(= temp658 temp678)
											(= temp628 temp658)
										)
									)
									(Print
										addTitle: @temp788
										addText: @temp508
										addText: @temp538 0 12
										addText: @temp568 0 24
										addText: @temp598 0 36
										addText: @temp628 0 48
										addText: @temp658 0 60
										addText: @temp678 0 72
										addText: @temp708 0 84
										addText: @temp738 0 96
										addText: @temp758 0 108
										init:
									)
									(if local1 (DlgWindow create:))
								else
									(Prints {No item to look at!})
								)
							)
							(4 (curItem editLoop:))
							(3 (curItem editLength:))
							(5 (curItem editLength:))
						)
					)
					(109 (self changeState: 0))
					(112
						(switch state
							(6 (DlgWindow editPosn:))
							(else  (curItem editPosn:))
						)
					)
					(116
						(switch state
							(6 (DlgWindow editTitle:))
							(else  (curItem editText:))
						)
					)
					(118
						(switch state
							(4 (curItem editView:))
							(1 (curItem editValue:))
						)
					)
					(119
						(switch state
							(0 (self changeState: 6))
							(5 (curItem editWidth:))
							(2 (curItem editWidth:))
						)
					)
					(120 (return (self exit:)))
				)
			)
		)
		(return 0)
	)
	
	(method (delItem &tmp [temp0 45])
		(if size
			(if
				(Print
					addText: {Delete current item?}
					font: 0
					addButton: 1 {Yes} 0 12
					addButton: 0 {No} 40 12
					init:
				)
				(self delete: curItem)
				(curItem dispose:)
				(= curItem 0)
				(if size (= curItem (self at: 0)))
			)
		else
			(Prints {Nothing to delete!})
		)
	)
	
	(method (exit &tmp [temp0 100] temp100 newFile temp102)
		(if (not local1) (DlgWindow create:))
		(if (not local2) (Format @local2 {%d.dlg} gRoomNumber))
		(= temp100
			(Print
				addTitle: @local42
				addText: {Save to:_}
				addEdit: @local2 25 60 0 @local2
				font: 0
				addButton: 1 {__Save__} 10 12
				addButton: 2 { Abandon_} 80 12
				addButton: 0 { Cancel_} 151 12
				init:
			)
		)
		(if (not temp100) (return 0))
		(if (== temp100 2) (return 1))
		(if (FileIO 10 @local2)
			(Format
				@temp0
				{The file '%s' already exists.__Now what?}
				@local2
			)
			(= temp100
				(Print
					addTitle: @local42
					addText: @temp0
					font: 0
					addButton: 1 { Replace_} 0 24
					addButton: 2 { Append_} 70 24
					addButton: 0 { Cancel_} 125 24
					init:
				)
			)
			(if (not temp100) (return 0))
		)
		(= temp102 (if (== temp100 1) 2 else 0))
		(= newFile (File new:))
		(if (not (newFile name: @local2 open: temp102))
			(Printf {Error opening '%s'} (newFile name?))
			(newFile dispose:)
			(return 0)
		)
		(newFile
			writeString: {\t\t; DialogEditor v1.0\n\n}
			writeString: {\t\t; by Brian K. Hughes\n\n}
			writeString: {\t\t(Print\n\n}
		)
		(if local1
			(Format
				@temp0
				{\t\t\tposn:\t\t\t%d %d,\n\n}
				(DlgWindow left?)
				(DlgWindow top?)
			)
			(newFile writeString: @temp0)
			(if (DlgWindow title?)
				(if (DlgWindow seq?)
					(Format
						@temp0
						{\t\t\taddTitle:\t%d %d %d %d %d,\n\n}
						(DlgWindow noun?)
						(DlgWindow verb?)
						(DlgWindow case?)
						(DlgWindow seq?)
						(DlgWindow modNum?)
					)
				else
					(Format
						@temp0
						{\t\t\taddTitle:\t\{%s\},\n\n}
						(DlgWindow title?)
					)
				)
				(newFile writeString: @temp0)
			)
		)
		(self writeFile: newFile)
		(if
			(Print
				addTitle: @local42
				addText: {This dialog should be...}
				font: 0
				addButton: 0 {___Modal___} 0 24
				addButton: 1 { Modeless_} 0 38
				init:
			)
			(newFile writeString: {\t\t\tmodeless:\tTRUE,\n\n})
		)
		(newFile
			writeString: {\t\t\tinit:\n\n}
			writeString: {\t\t)\n\n}
		)
		(newFile dispose:)
		(return 1)
	)
	
	(method (writeFile param1 &tmp temp0 temp1 [temp2 100] temp102 temp103 temp104)
		(= temp102 -1)
		(= temp103 0)
		(= temp104 0)
		(= temp0 0)
		(while (< temp0 size)
			(switch ((= temp1 (self at: temp0)) -super-?)
				(_DButton
					(if (!= (temp1 font?) temp102)
						(= temp102 (temp1 font?))
						(Format @temp2 {\t\t\tfont:\t\t\t%d,\n\n} temp102)
						(param1 writeString: @temp2)
					)
					(if (temp1 seq?)
						(Format
							@temp2
							{\t\t\taddButton:\t%d %d %d %d %d %d %d %d, \n\n}
							(temp1 value?)
							(temp1 noun?)
							(temp1 verb?)
							(temp1 case?)
							(temp1 seq?)
							(- (- (temp1 nsLeft?) (DlgWindow left?)) 4)
							(- (- (temp1 nsTop?) (DlgWindow top?)) 4)
							(temp1 modNum?)
						)
					else
						(Format
							@temp2
							{\t\t\taddButton:\t%d \{%s\} %d %d,\n\n}
							(temp1 value?)
							(temp1 text?)
							(- (- (temp1 nsLeft?) (DlgWindow left?)) 4)
							(- (- (temp1 nsTop?) (DlgWindow top?)) 4)
						)
					)
					(param1 writeString: @temp2)
				)
				(_DText
					(if (!= (temp1 font?) temp102)
						(= temp102 (temp1 font?))
						(Format @temp2 {\t\t\tfont:\t\t\t%d,\n\n} temp102)
						(param1 writeString: @temp2)
					)
					(if (!= (temp1 mode?) temp103)
						(= temp103 (temp1 mode?))
						(Format
							@temp2
							{\t\t\tmode:\t\t\t%s,\n\n}
							(switch (temp1 mode?)
								(0 {teJustLeft})
								(-1 {teJustRight})
								(1 {teJustCenter})
							)
						)
						(param1 writeString: @temp2)
					)
					(if (!= (temp1 width?) temp104)
						(= temp104 (temp1 width?))
						(Format @temp2 {\t\t\twidth:\t\t%d, \n\n} temp104)
						(param1 writeString: @temp2)
					)
					(if (temp1 seq?)
						(Format
							@temp2
							{\t\t\taddText:\t\t%d %d %d %d %d %d %d, \n\n}
							(temp1 noun?)
							(temp1 verb?)
							(temp1 case?)
							(temp1 seq?)
							(- (- (temp1 nsLeft?) (DlgWindow left?)) 4)
							(- (- (temp1 nsTop?) (DlgWindow top?)) 4)
							(temp1 modNum?)
						)
					else
						(Format
							@temp2
							{\t\t\taddText:\t\t\{%s\} %d %d,\n\n}
							(temp1 text?)
							(- (- (temp1 nsLeft?) (DlgWindow left?)) 4)
							(- (- (temp1 nsTop?) (DlgWindow top?)) 4)
						)
					)
					(param1 writeString: @temp2)
				)
				(_DEdit
					(if (!= (temp1 font?) temp102)
						(= temp102 (temp1 font?))
						(Format @temp2 {\t\t\tfont:\t\t\t%d,\n\n} temp102)
						(param1 writeString: @temp2)
					)
					(Format
						@temp2
						{\t\t\taddEdit:\t\t@str %d %d %d \{%s\},\n\n}
						(temp1 max?)
						(- (- (temp1 nsLeft?) (DlgWindow left?)) 4)
						(- (- (temp1 nsTop?) (DlgWindow top?)) 4)
						(temp1 text?)
					)
					(param1 writeString: @temp2)
				)
				(_DIcon
					(Format
						@temp2
						{\t\t\taddIcon:\t\t%d %d %d %d %d,\n\n}
						(temp1 view?)
						(temp1 loop?)
						(temp1 cel?)
						(- (- (temp1 nsLeft?) (DlgWindow left?)) 4)
						(- (- (temp1 nsTop?) (DlgWindow top?)) 4)
					)
					(param1 writeString: @temp2)
				)
			)
			(++ temp0)
		)
	)
	
	(method (changeState theState)
		(if curMenu (curMenu dispose:))
		(= curMenu
			(switch (= state theState)
				(0 mainMenu)
				(1 editBMenu)
				(2 editTMenu)
				(3 editEMenu)
				(4 editIMenu)
				(5 editSMenu)
				(6 editWMenu)
				(else  0)
			)
		)
		(if curMenu (curMenu init:))
	)
)

(instance mainMenu of ClickMenu
	(properties)
	
	(method (init)
		(super init: @local62)
	)
)

(instance editBMenu of ClickMenu
	(properties)
	
	(method (init)
		(super init: @local90)
	)
)

(instance editTMenu of ClickMenu
	(properties)
	
	(method (init)
		(super init: @local112)
	)
)

(instance editEMenu of ClickMenu
	(properties)
	
	(method (init)
		(super init: @local137)
	)
)

(instance editIMenu of ClickMenu
	(properties)
	
	(method (init)
		(super init: @local159)
	)
)

(instance editSMenu of ClickMenu
	(properties)
	
	(method (init)
		(super init: @local181)
	)
)

(instance editWMenu of ClickMenu
	(properties)
	
	(method (init)
		(super init: @local200)
	)
)
