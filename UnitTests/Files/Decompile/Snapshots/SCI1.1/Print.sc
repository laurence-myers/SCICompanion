;;; Sierra Script 1.0 - (do not remove this comment)
(script# 921)
(include sci.sh)
(use Main)
(use DColorButton)
(use Controls)
(use DialogControls)
(use System)

(public
	Prints 0
	Printf 1
	GetInput 2
	FindFormatLen 3
)

(procedure (Prints)
	(Print addText: &rest init:)
)

(procedure (Printf)
	(Print addTextF: &rest init:)
)

(procedure (GetInput param1 param2 param3 param4)
	(if
		(Print
			font: (if (> argc 3) param4 else gFont)
			addText: (if (and (> argc 2) param3) param3 else {})
			addEdit: param1 param2 0 12 param1
			init:
		)
		(StrLen param1)
	)
)

(procedure (FindFormatLen param1 param2 &tmp temp0 temp1 temp2 temp3)
	(= temp1 (StrLen param1))
	(= temp0 temp1)
	(= temp2 0)
	(= temp3 0)
	(while (< temp3 temp1)
		(if (== (StrAt param1 temp3) 37)
			(switch (StrAt param1 (++ temp3))
				(100 (= temp0 (+ temp0 5)))
				(120 (= temp0 (+ temp0 4)))
				(115
					(= temp0 (+ temp0 (StrLen [param2 temp2])))
				)
			)
			(++ temp2)
		)
		(++ temp3)
	)
	(return (++ temp0))
)

(class Print of Obj
	(properties
		dialog 0
		window 0
		title 0
		mode 0
		font -1
		width 0
		x -1
		y -1
		ticks 0
		caller 0
		retValue 0
		modeless 0
		first 0
		saveCursor 0
	)
	
	(method (init theCaller)
		(= caller 0)
		(if argc (= caller theCaller))
		(if (> argc 1) (self addText: &rest))
		(if (not modeless)
			(if (not (IsObject gPrints))
				(= gPrints ((EventHandler new:) name: {prints}))
			)
			(gPrints add: self)
		)
		(self showSelf:)
	)
	
	(method (doit)
		(dialog eachElementDo: 57)
	)
	
	(method (dispose)
		(if (and gPrints (gPrints contains: self))
			(gPrints delete: self)
			(if (gPrints isEmpty:)
				(gPrints dispose:)
				(= gPrints 0)
			)
		)
		(if title (Memory 3 title))
		(= width
			(= mode
				(= title (= first (= saveCursor (= window 0))))
			)
		)
		(= x (= y -1))
		(= modeless 0)
		(gSounds pause: 0)
		(super dispose:)
	)
	
	(method (showSelf &tmp theFirst temp1 temp2 temp3 temp4)
		(if saveCursor (gGame setCursor: 999))
		(if (not dialog) (= dialog (Dialog new:)))
		(dialog
			window: (if window else gWindow)
			name: {PODialog}
			caller: self
		)
		(dialog text: title time: ticks setSize:)
		(dialog center:)
		(= temp3 (if (== x -1) (dialog nsLeft?) else x))
		(= temp4 (if (== y -1) (dialog nsTop?) else y))
		(dialog moveTo: temp3 temp4)
		(= temp1 (GetPort))
		(dialog open: (if title 4 else 0) 15)
		(return
			(if modeless
				(= gOldPort (GetPort))
				(SetPort temp1)
				(= gDialog dialog)
			else
				(gSounds pause: 1)
				(= theFirst first)
				(cond 
					((not theFirst)
						(= theFirst (dialog firstTrue: 191 1))
						(if (and theFirst (not (dialog firstTrue: 191 2)))
							(theFirst state: (| (theFirst state?) $0002))
						)
					)
					((not (IsObject theFirst)) (= theFirst (dialog at: theFirst)))
				)
				(= retValue (dialog doit: theFirst))
				(SetPort temp1)
				(cond 
					((== retValue -1) (= retValue 0))
					(
					(and (IsObject retValue) (retValue isKindOf: DButton)) (= retValue (retValue value?)))
					((not (dialog theItem?)) (= retValue 1))
				)
				(if saveCursor
					(gGame setCursor: ((gIconBar curIcon?) cursor?))
				)
				(dialog dispose:)
				(return retValue)
			)
		)
	)
	
	(method (addButton param1 param2 param3 param4 param5 param6 param7 theTheGRoomNumber &tmp temp0 temp1 temp2 temp3 temp4 temp5 theGRoomNumber temp7 temp8)
		(if (not dialog) (= dialog (Dialog new:)))
		(if (== font -1) (= font gFont))
		(if (> argc 4)
			(= temp0 param2)
			(= temp1 param3)
			(= temp2 param4)
			(= temp3 (if param5 param5 else 1))
			(= temp4 0)
			(= temp5 0)
			(= theGRoomNumber gRoomNumber)
			(if (> argc 5)
				(= temp4 param6)
				(if (> argc 6)
					(= temp5 param7)
					(if (> argc 7) (= theGRoomNumber theTheGRoomNumber))
				)
			)
			(= temp8
				(Message 2 theGRoomNumber temp0 temp1 temp2 temp3)
			)
			(if temp8
				(= temp7
					(Memory
						1
						(= temp8
							(Message 2 theGRoomNumber temp0 temp1 temp2 temp3)
						)
					)
				)
				(if
					(not
						(Message 0 theGRoomNumber temp0 temp1 temp2 temp3 temp7)
					)
					(= temp7 0)
				)
			)
		else
			(= temp4 0)
			(= temp5 0)
			(if (> argc 2)
				(= temp4 param3)
				(if (> argc 3) (= temp5 param4))
			)
			(= temp7 (Memory 1 (+ (StrLen param2) 1)))
			(StrCpy temp7 param2)
		)
		(if temp7
			(dialog
				add:
					((DButton new:)
						value: param1
						font: font
						text: temp7
						setSize:
						moveTo: (+ 4 temp4) (+ 4 temp5)
						yourself:
					)
				setSize:
			)
		)
	)
	
	(method (addColorButton param1 param2 param3 param4 param5 param6 param7 theTheGRoomNumber param9 param10 param11 theTheTheTheGRoomNumber param13 param14 &tmp temp0 temp1 temp2 temp3 temp4 temp5 theGRoomNumber temp7 temp8 temp9 theTheTheGRoomNumber temp11 temp12 temp13 temp14)
		(if (not dialog) (= dialog (Dialog new:)))
		(if (== font -1) (= font gFont))
		(= temp9 0)
		(= temp11 15)
		(= temp13 31)
		(= theTheTheGRoomNumber 5)
		(= temp12 5)
		(= temp14 5)
		(if (< (Abs param2) 1000)
			(= temp0 param2)
			(= temp1 param3)
			(= temp2 param4)
			(= temp3 (if param5 param5 else 1))
			(= temp4 0)
			(= temp5 0)
			(= theGRoomNumber gRoomNumber)
			(if (> argc 5)
				(= temp4 param6)
				(if (> argc 6)
					(= temp5 param7)
					(if (> argc 7)
						(= theGRoomNumber theTheGRoomNumber)
						(if (> argc 8)
							(= temp9 param9)
							(if (> argc 9)
								(= temp11 param10)
								(if (> argc 10)
									(= temp13 param11)
									(if (> argc 11)
										(= theTheTheGRoomNumber theTheTheTheGRoomNumber)
										(if (> argc 12)
											(= temp12 param13)
											(if (> argc 13) (= temp14 param14))
										)
									)
								)
							)
						)
					)
				)
			)
			(= temp8
				(Message 2 theGRoomNumber temp0 temp1 temp2 temp3)
			)
			(if temp8
				(= temp7
					(Memory
						1
						(= temp8
							(Message 2 theGRoomNumber temp0 temp1 temp2 temp3)
						)
					)
				)
				(if
					(not
						(Message 0 theGRoomNumber temp0 temp1 temp2 temp3 temp7)
					)
					(= temp7 0)
				)
			)
		else
			(= temp4 0)
			(= temp5 0)
			(if (> argc 2)
				(= temp4 param3)
				(if (> argc 3)
					(= temp5 param4)
					(if (> argc 4)
						(= temp9 param5)
						(if (> argc 5)
							(= temp11 param6)
							(if (> argc 6)
								(= temp13 param7)
								(if (> argc 7)
									(= theTheTheGRoomNumber theTheGRoomNumber)
									(if (> argc 8)
										(= temp12 param9)
										(if (> argc 9) (= temp14 param10))
									)
								)
							)
						)
					)
				)
			)
			(= temp7 (Memory 1 (+ (StrLen param2) 1)))
			(StrCpy temp7 param2)
		)
		(if temp7
			(dialog
				add:
					((DColorButton new:)
						value: param1
						font: font
						text: temp7
						mode: mode
						nfc: temp9
						nbc: theTheTheGRoomNumber
						sfc: temp13
						sbc: temp14
						hfc: temp11
						hbc: temp12
						setSize: width
						moveTo: (+ 4 temp4) (+ 4 temp5)
						yourself:
					)
				setSize:
			)
		)
	)
	
	(method (addEdit param1 param2 param3 param4 param5 &tmp temp0 temp1)
		(if (not dialog) (= dialog (Dialog new:)))
		(StrCpy param1 (if (> argc 4) param5 else {}))
		(if (> argc 2)
			(= temp0 param3)
			(if (> argc 3) (= temp1 param4))
		)
		(dialog
			add:
				((DEdit new:)
					text: param1
					max: param2
					setSize:
					moveTo: (+ temp0 4) (+ temp1 4)
					yourself:
				)
			setSize:
		)
	)
	
	(method (addIcon param1 param2 param3 param4 param5 &tmp temp0 temp1)
		(if (not dialog) (= dialog (Dialog new:)))
		(if (> argc 3)
			(= temp0 param4)
			(= temp1 param5)
		else
			(= temp1 0)
			(= temp0 temp1)
		)
		(if (IsObject param1)
			(dialog
				add: (param1
					setSize:
					moveTo: (+ temp0 4) (+ temp1 4)
					yourself:
				)
				setSize:
			)
		else
			(dialog
				add:
					((DIcon new:)
						view: param1
						loop: param2
						cel: param3
						setSize:
						moveTo: (+ temp0 4) (+ temp1 4)
						yourself:
					)
				setSize:
			)
		)
	)
	
	(method (addText param1 param2 param3 param4 param5 param6 theTheGRoomNumber &tmp temp0 temp1 temp2 temp3 temp4 temp5 theGRoomNumber temp7 temp8)
		(if (not dialog) (= dialog (Dialog new:)))
		(if (== font -1) (= font gFont))
		(if (> argc 3)
			(= temp0 param1)
			(= temp1 param2)
			(= temp2 param3)
			(= temp3 (if param4 param4 else 1))
			(= temp4 0)
			(= temp5 0)
			(= theGRoomNumber gRoomNumber)
			(if (>= argc 5)
				(= temp4 param5)
				(if (>= argc 6)
					(= temp5 param6)
					(if (>= argc 7) (= theGRoomNumber theTheGRoomNumber))
				)
			)
			(= temp8
				(Message 2 theGRoomNumber temp0 temp1 temp2 temp3)
			)
			(if temp8
				(= temp7
					(Memory
						1
						(= temp8
							(Message 2 theGRoomNumber temp0 temp1 temp2 temp3)
						)
					)
				)
				(if
				(Message 0 theGRoomNumber temp0 temp1 temp2 temp3 temp7)
					(dialog
						add:
							((DText new:)
								text: temp7
								font: font
								mode: mode
								setSize: width
								moveTo: (+ 4 temp4) (+ 4 temp5)
								yourself:
							)
						setSize:
					)
				)
			)
		else
			(= temp4 0)
			(= temp5 0)
			(if (>= argc 2)
				(= temp4 param2)
				(if (>= argc 3) (= temp5 param3))
			)
			(= temp7 (Memory 1 (+ (StrLen param1) 1)))
			(StrCpy temp7 param1)
			(dialog
				add:
					((DText new:)
						text: temp7
						font: font
						mode: mode
						setSize: width
						moveTo: (+ 4 temp4) (+ 4 temp5)
						yourself:
					)
				setSize:
			)
		)
	)
	
	(method (addTextF &tmp temp0 temp1)
		(= temp0 (FindFormatLen &rest))
		(= temp1 (Memory 1 temp0))
		(Format temp1 &rest)
		(self addText: temp1)
		(Memory 3 temp1)
	)
	
	(method (addTitle param1 param2 param3 param4 param5 &tmp temp0 temp1 temp2 temp3 temp4 temp5)
		(if (> argc 1)
			(= temp0 param1)
			(= temp1 param2)
			(= temp2 param3)
			(= temp3 param4)
			(= temp4 param5)
			(= temp5 (Message 2 temp4 temp0 temp1 temp2 temp3))
			(if temp5
				(= title
					(Memory
						1
						(= temp5 (Message 2 temp4 temp0 temp1 temp2 temp3))
					)
				)
				(Message 0 temp4 temp0 temp1 temp2 temp3 title)
			)
		else
			(= title (Memory 1 (+ (StrLen param1) 1)))
			(StrCpy title param1)
		)
	)
	
	(method (posn theX theY)
		(= x theX)
		(= y theY)
	)
	
	(method (handleEvent param1)
		(if (dialog handleEvent: param1) (dialog dispose:))
	)
	
	(method (cue &tmp theCaller)
		(= theCaller caller)
		(= dialog 0)
		(if window (window dispose:))
		(self dispose:)
		(if theCaller (theCaller cue:))
	)
)
