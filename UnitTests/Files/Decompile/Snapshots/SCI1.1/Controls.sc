;;; Sierra Script 1.0 - (do not remove this comment)
(script# 255)
(include sci.sh)
(use Main)
(use Print)
(use System)

(public
	MouseStillDown 0
	GetNumber 1
)

(procedure (MouseStillDown &tmp newEvent temp1)
	(= newEvent (Event new:))
	(= temp1 (!= (newEvent type?) 2))
	(newEvent dispose:)
	(return temp1)
)

(procedure (GetNumber param1 param2 &tmp [temp0 40])
	(= temp0 0)
	(if (> argc 1) (Format @temp0 {%d} param2))
	(return
		(if (GetInput @temp0 5 param1)
			(ReadNumber @temp0)
		else
			-1
		)
	)
)

(class Control of Obj
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
	)
	
	(method (doit)
		(return value)
	)
	
	(method (enable param1)
		(if param1 (|= state $0001) else (&= state (~ $0001)))
	)
	
	(method (select param1)
		(if param1 (|= state $0008) else (&= state (~ $0008)))
		(self draw:)
	)
	
	(method (handleEvent param1 &tmp temp0 temp1 temp2)
		(if (param1 claimed?) (return 0))
		(= temp0 0)
		(if
			(and
				(& state $0001)
				(or
					(and
						(== (= temp1 (param1 type?)) 4)
						(== (param1 message?) key)
					)
					(and (== temp1 1) (self check: param1))
				)
			)
			(param1 claimed: 1)
			(= temp0 (self track: param1))
		)
		(return temp0)
	)
	
	(method (check param1)
		(return
			(and
				(>= (param1 x?) nsLeft)
				(>= (param1 y?) nsTop)
				(< (param1 x?) nsRight)
				(< (param1 y?) nsBottom)
			)
		)
	)
	
	(method (track param1 &tmp temp0 temp1)
		(return
			(if (== 1 (param1 type?))
				(= temp1 0)
				(repeat
					(= param1 (Event new: -32768))
					(param1 localize:)
					(= temp0 (self check: param1))
					(if (!= temp0 temp1)
						(HiliteControl self)
						(= temp1 temp0)
					)
					(param1 dispose:)
					(breakif(not (MouseStillDown)))
				)
				(if temp0 (HiliteControl self))
				(return temp0)
			else
				(return self)
			)
		)
	)
	
	(method (setSize)
	)
	
	(method (move theNsRight theNsTop)
		(+= nsRight theNsRight)
		(+= nsLeft theNsRight)
		(+= nsTop theNsTop)
		(+= nsBottom theNsTop)
	)
	
	(method (moveTo param1 param2)
		(self move: (- param1 nsLeft) (- param2 nsTop))
	)
	
	(method (draw)
		(DrawControl self)
	)
	
	(method (isType param1)
		(return (== type param1))
	)
	
	(method (checkState param1)
		(return (& state param1))
	)
	
	(method (cycle)
	)
)

(class DText of Control
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
		text 0
		font 1
		mode 0
		rects 0
	)
	
	(method (new &tmp temp0)
		((super new:) font: gFont yourself:)
	)
	
	(method (dispose param1)
		(if (and text (or (not argc) (not param1)))
			(Memory 3 (self text?))
		)
		(if rects (Memory 3 (self rects?)))
		(super dispose:)
	)
	
	(method (handleEvent param1 &tmp temp0 temp1 temp2 temp3 temp4)
		(if
			(and
				gTextCode
				rects
				(or
					(IsOneOf (param1 type?) 1 256)
					(and (== (param1 type?) 4) (== (param1 message?) 13))
				)
			)
			(= temp0 -1)
			(param1 globalize: claimed: 1)
			(while (!= (WordAt rects (+ temp0 1)) 30583)
				(= temp2 (WordAt rects (++ temp0)))
				(= temp1 (WordAt rects (++ temp0)))
				(= temp4 (WordAt rects (++ temp0)))
				(= temp3 (WordAt rects (++ temp0)))
				(if
					(and
						(<= temp2 (param1 x?))
						(<= (param1 x?) temp4)
						(<= temp1 (param1 y?))
						(<= (param1 y?) temp3)
					)
					(gTextCode doit: (/ temp0 4))
					(param1 type: 0 claimed: 0)
					(break)
				)
			)
		)
		(super handleEvent: param1)
	)
	
	(method (setSize param1 &tmp [temp0 2] temp2 temp3)
		(TextSize @temp0 text font (and argc param1))
		(= nsBottom (+ nsTop temp2))
		(= nsRight (+ nsLeft temp3))
	)
	
	(method (draw)
		(= rects (DrawControl self))
	)
)

(class Dialog of List
	(properties
		elements 0
		size 0
		text 0
		font 0
		window 0
		theItem 0
		nsTop 0
		nsLeft 0
		nsBottom 0
		nsRight 0
		time 0
		caller 0
		seconds 0
		lastSeconds 0
		eatTheMice 0
		lastTicks 0
	)
	
	(method (doit param1 &tmp temp0 temp1 temp2)
		(= gGameTime (+ gTickOffset (GetTime)))
		(= temp2 0)
		(self eachElementDo: 110)
		(if theItem (theItem select: 0))
		(= theItem
			(if (and argc param1)
				param1
			else
				(self firstTrue: 191 1)
			)
		)
		(if theItem (theItem select: 1))
		(if (not theItem)
			(= eatTheMice gEatTheMice)
			(= lastTicks (GetTime))
		else
			(= eatTheMice 0)
		)
		(= temp1 0)
		(while (not temp1)
			(= gGameTime (+ gTickOffset (GetTime)))
			(self eachElementDo: 192)
			(= temp0 ((Event new:) localize:))
			(if eatTheMice
				(-- eatTheMice)
				(if (== (temp0 type?) 1) (temp0 type: 0))
				(while (== lastTicks (GetTime))
				)
				(= lastTicks (GetTime))
			)
			(self eachElementDo: 96 checkHiliteCode self temp0)
			(= temp1 (self handleEvent: temp0))
			(temp0 dispose:)
			(if (self check:) (break))
			(if (== temp1 -2) (break))
			(Wait 1)
		)
		(return temp1)
	)
	
	(method (dispose &tmp theCaller)
		(self eachElementDo: 111 release:)
		(if (== self gDialog)
			(SetPort gOldPort)
			(= gDialog 0)
			(= gOldPort 0)
		)
		(if window (window dispose:) (= window 0))
		(= theItem 0)
		(= theCaller caller)
		(super dispose:)
		(if theCaller (theCaller cue:))
	)
	
	(method (open param1 param2)
		(if (and (PicNotValid) gCast)
			(Animate (gCast elements?) 0)
		)
		(= window (window new:))
		(window
			top: nsTop
			left: nsLeft
			bottom: nsBottom
			right: nsRight
			title: text
			type: param1
			priority: param2
			open:
		)
		(= seconds time)
		(self draw:)
	)
	
	(method (draw)
		(self eachElementDo: 80)
	)
	
	(method (advance &tmp temp0 dialogFirst)
		(if theItem
			(theItem select: 0)
			(= dialogFirst (self contains: theItem))
			(repeat
				(= dialogFirst (self next: dialogFirst))
				(if (not dialogFirst) (= dialogFirst (self first:)))
				(= theItem (NodeValue dialogFirst))
				(if (& (theItem state?) $0001) (break))
			)
			(theItem select: 1)
			(gGame
				setCursor:
					gCursorNumber
					1
					(+
						(theItem nsLeft?)
						(/ (- (theItem nsRight?) (theItem nsLeft?)) 2)
					)
					(- (theItem nsBottom?) 3)
			)
		)
	)
	
	(method (retreat &tmp temp0 dialogLast)
		(if theItem
			(theItem select: 0)
			(= dialogLast (self contains: theItem))
			(repeat
				(= dialogLast (self prev: dialogLast))
				(if (not dialogLast) (= dialogLast (self last:)))
				(= theItem (NodeValue dialogLast))
				(if (& (theItem state?) $0001) (break))
			)
			(theItem select: 1)
			(gGame
				setCursor:
					gCursorNumber
					1
					(+
						(theItem nsLeft?)
						(/ (- (theItem nsRight?) (theItem nsLeft?)) 2)
					)
					(- (theItem nsBottom?) 3)
			)
		)
	)
	
	(method (move theNsRight theNsTop)
		(+= nsRight theNsRight)
		(+= nsLeft theNsRight)
		(+= nsTop theNsTop)
		(+= nsBottom theNsTop)
	)
	
	(method (moveTo param1 param2)
		(self move: (- param1 nsLeft) (- param2 nsTop))
	)
	
	(method (center)
		(self
			moveTo:
				(+
					(window brLeft?)
					(/
						(-
							(- (window brRight?) (window brLeft?))
							(- nsRight nsLeft)
						)
						2
					)
				)
				(+
					(window brTop?)
					(/
						(-
							(- (window brBottom?) (window brTop?))
							(- nsBottom nsTop)
						)
						2
					)
				)
		)
	)
	
	(method (setSize &tmp dialogFirst temp1 theNsTop theNsLeft theNsBottom theNsRight)
		(if text
			(TextSize @theNsTop text font -1 0)
			(= nsTop theNsTop)
			(= nsLeft theNsLeft)
			(= nsBottom theNsBottom)
			(= nsRight theNsRight)
		else
			(= nsRight (= nsBottom (= nsLeft (= nsTop 0))))
		)
		(= dialogFirst (self first:))
		(while dialogFirst
			(= temp1 (NodeValue dialogFirst))
			(if (< (temp1 nsLeft?) nsLeft)
				(= nsLeft (temp1 nsLeft?))
			)
			(if (< (temp1 nsTop?) nsTop) (= nsTop (temp1 nsTop?)))
			(if (> (temp1 nsRight?) nsRight)
				(= nsRight (temp1 nsRight?))
			)
			(if (> (temp1 nsBottom?) nsBottom)
				(= nsBottom (temp1 nsBottom?))
			)
			(= dialogFirst (self next: dialogFirst))
		)
		(+= nsRight 4)
		(+= nsBottom 4)
		(self moveTo: 0 0)
	)
	
	(method (handleEvent param1 &tmp theTheItem temp1 temp2)
		(if (& (param1 type?) $0040)
			(switch (param1 message?)
				(5
					(param1 type: 4 message: 20480)
				)
				(1
					(param1 type: 4 message: 18432)
				)
				(7
					(param1 type: 4 message: 19200)
				)
				(3
					(param1 type: 4 message: 19712)
				)
			)
		)
		(= temp1 (param1 type?))
		(= temp2 (param1 message?))
		(= theTheItem (self firstTrue: 133 param1))
		(if theTheItem
			(EditControl theItem 0)
			(if (not (theTheItem checkState: 2))
				(if theItem (theItem select: 0))
				((= theItem theTheItem) select: 1)
				(theTheItem doit:)
				(= theTheItem 0)
			else
				(return theTheItem)
			)
		else
			(= temp1 (param1 type?))
			(= temp2 (param1 message?))
			(= theTheItem 0)
			(cond 
				(
					(and
						(or (== temp1 256) (and (== temp1 4) (== temp2 13)))
						theItem
						(theItem checkState: 1)
					)
					(= theTheItem theItem)
					(EditControl theItem 0)
					(param1 claimed: 1)
				)
				((and (== temp1 4) (== temp2 27)) (param1 claimed: 1) (= theTheItem -1))
				(
					(and
						(not (self firstTrue: 191 1))
						(or
							(and (== temp1 4) (== temp2 13))
							(IsOneOf temp1 1 256)
						)
					)
					(param1 claimed: 1)
					(= theTheItem -2)
				)
				(
					(and
						(IsObject theItem)
						(theItem isType: 3)
						(== temp1 4)
						(== temp2 19712)
					)
					(if
					(>= (theItem cursor?) (StrLen (theItem text?)))
						(self advance:)
					else
						(EditControl theItem param1)
					)
				)
				(
					(and
						(IsObject theItem)
						(theItem isType: 3)
						(== temp1 4)
						(== temp2 19200)
					)
					(if (<= (theItem cursor?) 0)
						(self retreat:)
					else
						(EditControl theItem param1)
					)
				)
				(
				(and (== temp1 4) (IsOneOf temp2 9 19712 20480)) (param1 claimed: 1) (self advance:))
				(
				(and (== temp1 4) (IsOneOf temp2 3840 19200 18432)) (param1 claimed: 1) (self retreat:))
				(else (EditControl theItem param1))
			)
		)
		(return theTheItem)
	)
	
	(method (check &tmp theLastSeconds)
		(return
			(if
				(and
					seconds
					(!= lastSeconds (= theLastSeconds (GetTime 1)))
				)
				(= lastSeconds theLastSeconds)
				(return (not (-- seconds)))
			else
				0
			)
		)
	)
)

(instance checkHiliteCode of Code
	(properties)
	
	(method (doit param1 param2 param3)
		(if
			(and
				(& (param1 state?) $0001)
				(param1 check: param3)
				(not (& (param1 state?) $0008))
			)
			((param2 theItem?) select: 0)
			(param2 theItem: param1)
			(param1 select: 1)
		)
	)
)
