;;; Sierra Script 1.0 - (do not remove this comment)
(script# 937)
(include sci.sh)
(use Main)
(use Print)
(use System)


(class IconItem of Obj
	(properties
		view -1
		loop -1
		cel -1
		nsLeft 0
		nsTop -1
		nsRight 0
		nsBottom 0
		state 0
		cursor -1
		type 16384
		message -1
		modifiers 0
		signal 1
		maskView 0
		maskLoop 0
		maskCel 0
		highlightColor 0
		lowlightColor 0
		noun 0
		modNum 0
		helpVerb 0
	)
	
	(method (show theNsLeft theNsTop &tmp [temp0 7])
		(|= signal $0020)
		(if argc
			(= nsRight
				(+ (= nsLeft theNsLeft) (CelWide view loop cel))
			)
			(= nsBottom
				(+ (= nsTop theNsTop) (CelHigh view loop cel))
			)
		else
			(= nsRight (+ nsLeft (CelWide view loop cel)))
			(= nsBottom (+ nsTop (CelHigh view loop cel)))
		)
		(DrawCel view loop cel nsLeft nsTop -1)
		(if (& signal $0004) (self mask:))
		(if
		(and gPseudoMouse (gPseudoMouse respondsTo: 173))
			(gPseudoMouse stop:)
		)
	)
	
	(method (select param1 &tmp newEvent temp1 gGameScript)
		(return
			(cond 
				((& signal $0004) 0)
				((and argc param1 (& signal $0001))
					(= temp1 1)
					(DrawCel view loop temp1 nsLeft nsTop -1)
					(Graph 12 nsTop nsLeft nsBottom nsRight 1)
					(while (!= ((= newEvent (Event new:)) type?) 2)
						(newEvent localize:)
						(cond 
							((self onMe: newEvent)
								(if (not temp1)
									(= temp1 1)
									(DrawCel view loop temp1 nsLeft nsTop -1)
									(Graph 12 nsTop nsLeft nsBottom nsRight 1)
								)
							)
							(temp1
								(= temp1 0)
								(DrawCel view loop temp1 nsLeft nsTop -1)
								(Graph 12 nsTop nsLeft nsBottom nsRight 1)
							)
						)
						(newEvent dispose:)
					)
					(newEvent dispose:)
					(if (== temp1 1)
						(DrawCel view loop 0 nsLeft nsTop -1)
						(Graph 12 nsTop nsLeft nsBottom nsRight 1)
					)
					(= gGameScript (gGame script?))
					temp1
				)
				(else (= gGameScript (gGame script?)) 1)
			)
		)
	)
	
	(method (highlight param1 &tmp temp0 temp1 temp2 temp3 temp4)
		(if
		(or (not (& signal $0020)) (== highlightColor -1))
			(return)
		)
		(= temp4
			(if (and argc param1) highlightColor else lowlightColor)
		)
		(= temp0 (+ nsTop 2))
		(= temp1 (+ nsLeft 2))
		(= temp2 (- nsBottom 3))
		(= temp3 (- nsRight 4))
		(Graph 4 temp0 temp1 temp0 temp3 temp4 -1 -1)
		(Graph 4 temp0 temp3 temp2 temp3 temp4 -1 -1)
		(Graph 4 temp2 temp3 temp2 temp1 temp4 -1 -1)
		(Graph 4 temp2 temp1 temp0 temp1 temp4 -1 -1)
		(Graph
			12
			(- nsTop 2)
			(- nsLeft 2)
			nsBottom
			(+ nsRight 3)
			1
		)
	)
	
	(method (onMe param1)
		(return
			(and
				(>= (param1 x?) nsLeft)
				(>= (param1 y?) nsTop)
				(<= (param1 x?) nsRight)
				(<= (param1 y?) nsBottom)
			)
		)
	)
	
	(method (mask)
		(DrawCel
			maskView
			maskLoop
			maskCel
			(+
				nsLeft
				(/
					(-
						(CelWide view loop cel)
						(CelWide maskView maskLoop maskCel)
					)
					2
				)
			)
			(+
				nsTop
				(/
					(-
						(CelHigh view loop cel)
						(CelHigh maskView maskLoop maskCel)
					)
					2
				)
			)
			-1
		)
	)
)

(class IconBar of Set
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
	)
	
	(method (doit &tmp temp0 temp1 temp2 temp3 gGameScript)
		(while
			(and
				(& state $0020)
				(= temp0 ((gUser curEvent?) new:))
			)
			(= temp1 (temp0 type?))
			(= temp2 (temp0 message?))
			(= temp3 (temp0 modifiers?))
			(Wait 1)
			(= gGameTime (+ gTickOffset (GetTime)))
			(if gCuees (gCuees eachElementDo: 57))
			(= gGameScript (gGame script?))
			(if (== temp1 256)
				(= temp1 4)
				(= temp2 (if (& temp3 $0003) 27 else 13))
				(= temp3 0)
				(temp0 type: temp1 message: temp2 modifiers: temp3)
			)
			(temp0 localize:)
			(if
				(and
					(or (== temp1 1) (and (== temp1 4) (== temp2 13)))
					(IsObject helpIconItem)
					(& (helpIconItem signal?) $0010)
				)
				(temp0 type: 24576 message: (helpIconItem message?))
			)
			(MapKeyToDir temp0)
			(if (self dispatchEvent: temp0) (break))
		)
	)
	
	(method (handleEvent param1 &tmp temp0 temp1 temp2 temp3 theGCursorNumber theCurIcon theCurInvIcon)
		(param1 localize:)
		(= temp1 (param1 type?))
		(cond 
			((& state $0004))
			(
				(or
					(and
						(not temp1)
						(& state $0400)
						(<= -10 (param1 y?))
						(<= (param1 y?) height)
						(<= 0 (param1 x?))
						(<= (param1 x?) 320)
						(not (= temp0 0))
					)
					(and
						(== temp1 4)
						(or
							(== (param1 message?) 27)
							(== (param1 message?) 21248)
						)
						(= temp0 1)
					)
				)
				(param1 globalize:)
				(= oldMouseX (param1 x?))
				(= oldMouseY (param1 y?))
				(= theGCursorNumber gCursorNumber)
				(= theCurIcon curIcon)
				(= theCurInvIcon curInvIcon)
				(self show:)
				(gGame setCursor: 999)
				(if temp0
					(gGame
						setCursor:
							gCursorNumber
							1
							(+
								(curIcon nsLeft?)
								(/ (- (curIcon nsRight?) (curIcon nsLeft?)) 2)
							)
							(- (curIcon nsBottom?) 3)
					)
				)
				(self doit:)
				(= temp3
					(if (or (gUser canControl:) (gUser canInput:))
						(curIcon cursor?)
					else
						gWaitCursor
					)
				)
				(if temp0
					(gGame setCursor: temp3 1 oldMouseX oldMouseY)
				else
					(gGame
						setCursor: temp3 1 ((param1 new:) x?) (Max (param1 y?) (+ 1 height))
					)
				)
				(self hide:)
			)
			((& temp1 $0004)
				(switch (param1 message?)
					(13
						(cond 
							((not (IsObject curIcon)))
							((or (!= curIcon useIconItem) curInvIcon)
								(param1
									type: (curIcon type?)
									message:
										(if (== curIcon useIconItem)
											(curInvIcon message?)
										else
											(curIcon message?)
										)
								)
							)
							(else (param1 type: 0))
						)
					)
					(20992
						(if (gUser canControl:) (self swapCurIcon:))
						(param1 claimed: 1)
					)
					(0
						(if (& (param1 type?) $0040)
							(self advanceCurIcon:)
							(param1 claimed: 1)
						)
					)
				)
			)
			((& temp1 $0001)
				(cond 
					((& (param1 modifiers?) $0003) (self advanceCurIcon:) (param1 claimed: 1))
					((& (param1 modifiers?) $0004)
						(if (gUser canControl:) (self swapCurIcon:))
						(param1 claimed: 1)
					)
					((IsObject curIcon)
						(param1
							type: (curIcon type?)
							message:
								(if (== curIcon useIconItem)
									(curInvIcon message?)
								else
									(curIcon message?)
								)
						)
					)
				)
			)
		)
	)
	
	(method (show &tmp temp0 temp1 temp2 temp3 theY temp5 temp6 temp7)
		(gSounds pause:)
		(|= state $0020)
		(gGame setCursor: 999 1)
		(= temp0 (self at: 0))
		(= height
			(CelHigh (temp0 view?) (temp0 loop?) (temp0 cel?))
		)
		(= port (GetPort))
		(SetPort -1)
		(= underBits (Graph 7 y 0 (+ y height) 320 1))
		(= temp1 (PicNotValid))
		(PicNotValid 1)
		(= temp3 0)
		(= theY y)
		(= temp5 (FirstNode elements))
		(while temp5
			(= temp6 (NextNode temp5))
			(= temp7 (NodeValue temp5))
			(if (not (IsObject temp7)) (return))
			(if (<= (temp7 nsRight?) 0)
				(temp7 show: temp3 theY)
				(= temp3 (temp7 nsRight?))
			else
				(temp7 show:)
			)
			(= temp5 temp6)
		)
		(if curInvIcon
			(if (gEgo has: (gInv indexOf: curInvIcon))
				(= temp3
					(+
						(/
							(-
								(- (useIconItem nsRight?) (useIconItem nsLeft?))
								(CelWide
									(curInvIcon view?)
									(curInvIcon loop?)
									(curInvIcon cel?)
								)
							)
							2
						)
						(useIconItem nsLeft?)
					)
				)
				(= theY
					(+
						y
						(/
							(-
								(- (useIconItem nsBottom?) (useIconItem nsTop?))
								(CelHigh
									(curInvIcon view?)
									(curInvIcon loop?)
									(curInvIcon cel?)
								)
							)
							2
						)
						(useIconItem nsTop?)
					)
				)
				(DrawCel
					(curInvIcon view?)
					(curInvIcon loop?)
					(curInvIcon cel?)
					temp3
					theY
					-1
				)
				(if (& (useIconItem signal?) $0004)
					(useIconItem mask:)
				)
			else
				(= curInvIcon 0)
			)
		)
		(PicNotValid temp1)
		(Graph 12 y 0 (+ y height) 320 1)
		(self highlight: curIcon)
	)
	
	(method (hide &tmp temp0 temp1 temp2)
		(if (& state $0020)
			(gSounds pause: 0)
			(&= state $ffdf)
			(= temp0 (FirstNode elements))
			(while temp0
				(= temp1 (NextNode temp0))
				(= temp2 (NodeValue temp0))
				(if (not (IsObject temp2)) (return))
				(= temp2 (NodeValue temp0))
				(temp2 signal: (& (temp2 signal?) (~ $0020)))
				(= temp0 temp1)
			)
			(if
				(and
					(not (& state $0800))
					(IsObject helpIconItem)
					(& (helpIconItem signal?) $0010)
				)
				(helpIconItem signal: (& (helpIconItem signal?) $ffef))
			)
			(Graph 8 underBits)
			(Graph 12 y 0 (+ y height) 320 1)
			(Graph 13 y 0 (+ y height) 320)
			(SetPort port)
			(= height activateHeight)
		)
	)
	
	(method (advance &tmp temp0 temp1)
		(= temp1 1)
		(while (<= temp1 size)
			(= temp0
				(self
					at: (mod (+ temp1 (self indexOf: highlightedIcon)) size)
				)
			)
			(if (not (IsObject temp0))
				(= temp0 (NodeValue (self first:)))
			)
			(if (not (& (temp0 signal?) $0004)) (break))
			(= temp1 (mod (+ temp1 1) size))
		)
		(self highlight: temp0 (& state $0020))
	)
	
	(method (retreat &tmp temp0 temp1)
		(= temp1 1)
		(while (<= temp1 size)
			(= temp0
				(self
					at: (mod (- (self indexOf: highlightedIcon) temp1) size)
				)
			)
			(if (not (IsObject temp0))
				(= temp0 (NodeValue (self last:)))
			)
			(if (not (& (temp0 signal?) $0004)) (break))
			(= temp1 (mod (+ temp1 1) size))
		)
		(self highlight: temp0 (& state $0020))
	)
	
	(method (select theCurIcon param2)
		(return
			(if (theCurIcon select: (and (>= argc 2) param2))
				(if (not (& (theCurIcon signal?) $0002))
					(= curIcon theCurIcon)
				)
				1
			else
				0
			)
		)
	)
	
	(method (highlight theHighlightedIcon param2 &tmp temp0)
		(if (not (& (theHighlightedIcon signal?) $0004))
			(if (IsObject highlightedIcon)
				(highlightedIcon highlight: 0)
			)
			((= highlightedIcon theHighlightedIcon) highlight: 1)
		)
		(if (and (>= argc 2) param2)
			(gGame
				setCursor:
					gCursorNumber
					1
					(+
						(theHighlightedIcon nsLeft?)
						(/
							(-
								(theHighlightedIcon nsRight?)
								(theHighlightedIcon nsLeft?)
							)
							2
						)
					)
					(- (theHighlightedIcon nsBottom?) 3)
			)
		)
	)
	
	(method (swapCurIcon &tmp temp0)
		(if (& state $0004)
			(return)
		else
			(= temp0 (NodeValue (self first:)))
			(cond 
				(
					(and
						(!= curIcon temp0)
						(not (& (temp0 signal?) $0004))
					)
					(= prevIcon curIcon)
					(= curIcon (NodeValue (self first:)))
				)
				(
				(and prevIcon (not (& (prevIcon signal?) $0004))) (= curIcon prevIcon))
			)
		)
		(gGame setCursor: (curIcon cursor?) 1)
	)
	
	(method (advanceCurIcon &tmp theCurIcon temp1 temp2)
		(if (& state $0004) (return))
		(= theCurIcon curIcon)
		(= temp1 0)
		(while
			(&
				((= theCurIcon
					(self at: (mod (+ (self indexOf: theCurIcon) 1) size))
				)
					signal?
				)
				$0006
			)
			(if (> temp1 (+ 1 size)) (return) else (++ temp1))
		)
		(= curIcon theCurIcon)
		(gGame setCursor: (curIcon cursor?) 1)
	)
	
	(method (dispatchEvent param1 &tmp temp0 temp1 temp2 temp3 theHighlightedIcon temp5 temp6 [temp7 50] temp57 theHighlightedIconSignal temp59 temp60)
		(= temp1 (param1 x?))
		(= temp0 (param1 y?))
		(= temp2 (param1 type?))
		(= temp3 (param1 message?))
		(= temp5 (param1 claimed?))
		(= theHighlightedIcon (self firstTrue: 226 param1))
		(if theHighlightedIcon
			(= temp57
				((= theHighlightedIcon (self firstTrue: 226 param1))
					cursor?
				)
			)
			(= theHighlightedIconSignal (theHighlightedIcon signal?))
			(= temp59 (== theHighlightedIcon helpIconItem))
		)
		(if (& temp2 $0040)
			(switch temp3
				(3 (self advance:))
				(7 (self retreat:))
			)
		else
			(switch temp2
				(0
					(cond 
						(
							(not
								(and
									(<= 0 temp0)
									(<= temp0 (+ y height))
									(<= 0 temp1)
									(<= temp1 320)
								)
							)
							(if
								(and
									(& state $0400)
									(or
										(not (IsObject helpIconItem))
										(not (& (helpIconItem signal?) $0010))
									)
								)
								(= oldMouseY 0)
								(= temp5 1)
							)
						)
						(
							(and
								theHighlightedIcon
								(!= theHighlightedIcon highlightedIcon)
							)
							(= oldMouseY 0)
							(self highlight: theHighlightedIcon)
						)
					)
				)
				(1
					(if
						(and
							theHighlightedIcon
							(self select: theHighlightedIcon 1)
						)
						(if temp59
							(if temp57 (gGame setCursor: temp57))
							(if (& state $0800)
								(self noClickHelp:)
							else
								(helpIconItem signal: (| (helpIconItem signal?) $0010))
							)
						else
							(= temp5 (& theHighlightedIconSignal $0040))
						)
						(theHighlightedIcon doit:)
					)
				)
				(4
					(switch temp3
						(27 (= temp5 1))
						(21248 (= temp5 1))
						(13
							(if (not theHighlightedIcon)
								(= theHighlightedIcon highlightedIcon)
							)
							(cond 
								(
									(and
										theHighlightedIcon
										(== theHighlightedIcon helpIconItem)
									)
									(if (!= temp57 -1) (gGame setCursor: temp57))
									(if helpIconItem
										(helpIconItem signal: (| (helpIconItem signal?) $0010))
									)
								)
								(
									(and
										(IsObject theHighlightedIcon)
										(self select: theHighlightedIcon)
									)
									(theHighlightedIcon doit:)
									(= temp5 (& theHighlightedIconSignal $0040))
								)
							)
						)
						(3840 (self retreat:))
						(9 (self advance:))
					)
				)
				(24576
					(if
					(and theHighlightedIcon (theHighlightedIcon helpVerb?))
						(if (not (HaveMouse))
							(= temp60 (gGame setCursor: 996))
						)
						(= temp6 (GetPort))
						(Print
							font: gFont
							width: 250
							addText:
								(theHighlightedIcon noun?)
								(theHighlightedIcon helpVerb?)
								0
								1
								0
								0
								(theHighlightedIcon modNum?)
							init:
						)
						(SetPort temp6)
						(if (not (HaveMouse)) (gGame setCursor: temp60))
					)
					(if helpIconItem
						(helpIconItem signal: (& (helpIconItem signal?) $ffef))
					)
					(gGame setCursor: 999)
				)
			)
		)
		(return temp5)
	)
	
	(method (disable param1 &tmp temp0 temp1)
		(if argc
			(= temp0 0)
			(while (< temp0 argc)
				(= temp1
					(if (IsObject [param1 temp0])
						[param1 temp0]
					else
						(self at: [param1 temp0])
					)
				)
				(temp1 signal: (| (temp1 signal?) $0004))
				(cond 
					((== temp1 curIcon) (self advanceCurIcon:))
					((== temp1 highlightedIcon) (self advance:))
				)
				(++ temp0)
			)
		else
			(|= state $0004)
		)
	)
	
	(method (enable param1 &tmp temp0 temp1)
		(if argc
			(= temp0 0)
			(while (< temp0 argc)
				(= temp1
					(if (IsObject [param1 temp0])
						[param1 temp0]
					else
						(self at: [param1 temp0])
					)
				)
				(temp1 signal: (& (temp1 signal?) (~ $0004)))
				(++ temp0)
			)
		else
			(&= state $fffb)
		)
	)
	
	(method (noClickHelp &tmp temp0 temp1 temp2 temp3 gWindowEraseOnly temp5)
		(= temp2 0)
		(= temp1 temp2)
		(= temp3 (GetPort))
		(= gWindowEraseOnly (gWindow eraseOnly?))
		(gWindow eraseOnly: 1)
		(while
		(not ((= temp0 ((gUser curEvent?) new:)) type?))
			(if (not (self isMemberOf: IconBar)) (temp0 localize:))
			(= temp2 (self firstTrue: 226 temp0))
			(cond 
				(temp2
					(if
						(and
							(!= (= temp2 (self firstTrue: 226 temp0)) temp1)
							(temp2 helpVerb?)
						)
						(= temp1 temp2)
						(if gDialog (gDialog dispose:))
						(Print
							font: gFont
							width: 250
							addText: (temp2 noun?) (temp2 helpVerb?) 0 1 0 0 (temp2 modNum?)
							modeless: 1
							init:
						)
						(SetPort temp3)
					)
				)
				(gDialog (gDialog dispose:))
				(else (= temp1 0))
			)
			(temp0 dispose:)
		)
		(gWindow eraseOnly: gWindowEraseOnly)
		(gGame setCursor: 999 1)
		(if gDialog (gDialog dispose:))
		(SetPort temp3)
		(if (not (helpIconItem onMe: temp0))
			(self dispatchEvent: temp0)
		)
	)
	
	(method (findIcon param1 &tmp temp0 temp1)
		(= temp0 0)
		(while (< temp0 size)
			(= temp1 (self at: temp0))
			(if (== (temp1 message?) param1) (return temp1))
			(++ temp0)
		)
		(return 0)
	)
)
