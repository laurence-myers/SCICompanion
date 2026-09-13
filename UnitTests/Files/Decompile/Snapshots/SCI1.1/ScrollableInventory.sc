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
	
	(method (doit &tmp theCurIcon temp1 temp2 temp3 temp4 [temp5 2] gWindowEraseOnly temp8 temp9 [temp10 51])
		(while ((= temp1 ((gUser curEvent?) new:)) type?)
		)
		(while (& state $0020)
			(= gPEventX ((= temp1 ((gUser curEvent?) new:)) x?))
			(= gPEventY (temp1 y?))
			(= temp2 (temp1 type?))
			(= temp3 (temp1 message?))
			(= temp4 (temp1 modifiers?))
			(= temp9 0)
			(temp1 localize:)
			(if
				(and
					curIcon
					(not temp4)
					(!= curIcon selectIcon)
					(or
						(== temp2 1)
						(and (== temp2 4) (== temp3 13) (= temp9 1))
						(and (== temp2 256) (= temp9 1))
					)
					(or
						(!= curIcon helpIconItem)
						(& (helpIconItem signal?) $0010)
					)
				)
				(temp1 type: 16384 message: (curIcon message?))
			)
			(MapKeyToDir temp1)
			(= temp2 (temp1 type?))
			(= temp3 (temp1 message?))
			(if gCuees (gCuees eachElementDo: 57))
			(if gFastCast (gFastCast handleEvent: temp1) (continue))
			(if (and (== temp2 1) temp4)
				(self advanceCurIcon:)
				(temp1 claimed: 1)
				(continue)
			)
			(if
				(and
					(== temp2 0)
					(= theCurIcon (self firstTrue: 226 temp1))
					(!= theCurIcon highlightedIcon)
				)
				(self highlight: theCurIcon)
				(continue)
			)
			(cond 
				(
					(or
						(== temp2 1)
						(and (== temp2 4) (== temp3 13))
						(== temp2 256)
					)
					(if
						(and
							(IsObject highlightedIcon)
							(self select: highlightedIcon (== temp2 1))
						)
						(if (== highlightedIcon okButton) (break))
						(if (== highlightedIcon helpIconItem)
							(if (!= (highlightedIcon cursor?) -1)
								(gGame setCursor: (helpIconItem cursor?))
							)
							(if (& state $0800) (self noClickHelp:) (continue))
							(if helpIconItem
								(helpIconItem signal: (| (helpIconItem signal?) $0010))
								(continue)
							)
						else
							(= curIcon highlightedIcon)
							(gGame setCursor: (curIcon cursor?))
							(continue)
						)
					)
				)
				((& temp2 $0040)
					(switch temp3
						(3 (self advance:))
						(7 (self retreat:))
						(1
							(if
								(and
									highlightedIcon
									(= theCurIcon
										(localproc_00de
											highlightedIcon
											(- (highlightedIcon nsTop?) 1)
											0
										)
									)
								)
								(self highlight: theCurIcon 1)
							else
								(self retreat:)
							)
						)
						(5
							(if
								(and
									highlightedIcon
									(= theCurIcon
										(localproc_00de
											highlightedIcon
											(+ (highlightedIcon nsBottom?) 1)
											(window bottom?)
										)
									)
								)
								(self highlight: theCurIcon 1)
							else
								(self advance:)
							)
						)
						(0
							(if (& temp2 $0004) (self advanceCurIcon:))
						)
					)
				)
				((== temp2 4)
					(switch temp3
						(9 (self advance:))
						(3840 (self retreat:))
						(27 (break))
					)
				)
				(
					(and
						(& temp2 $4000)
						(= theCurIcon (self firstTrue: 226 temp1))
					)
					(if (& temp2 $2000)
						(if
							(and
								theCurIcon
								(theCurIcon noun?)
								(Message
									0
									(theCurIcon modNum?)
									(theCurIcon noun?)
									(theCurIcon helpVerb?)
									0
									1
									@temp10
								)
							)
							(if (gWindow respondsTo: 244)
								(= gWindowEraseOnly (gWindow eraseOnly?))
								(gWindow eraseOnly: 1)
								(Prints @temp10)
								(gWindow eraseOnly: gWindowEraseOnly)
							else
								(Prints @temp10)
							)
						)
						(helpIconItem signal: (& (helpIconItem signal?) $ffef))
						(gGame setCursor: 999)
						(continue)
					)
					(if (== theCurIcon okButton) (break))
					(cond 
						((not (theCurIcon isKindOf: InventoryItem))
							(if (self select: theCurIcon (not temp9))
								(= curIcon theCurIcon)
								(gGame setCursor: (curIcon cursor?))
								(if (== theCurIcon helpIconItem)
									(if (& state $0800) (self noClickHelp:) (continue))
									(helpIconItem signal: (| (helpIconItem signal?) $0010))
									(continue)
								)
							)
						)
						(curIcon
							(if (gWindow respondsTo: 244)
								(= gWindowEraseOnly (gWindow eraseOnly?))
								(gWindow eraseOnly: 1)
							)
							(if (curIcon isKindOf: InventoryItem)
								(theCurIcon doVerb: (curIcon message?))
							else
								(theCurIcon doVerb: (temp1 message?))
							)
							(if (gWindow respondsTo: 244)
								(gWindow eraseOnly: gWindowEraseOnly)
							)
						)
					)
				)
			)
		)
		(self hide:)
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
