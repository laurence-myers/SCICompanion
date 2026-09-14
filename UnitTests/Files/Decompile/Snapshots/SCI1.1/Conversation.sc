;;; Sierra Script 1.0 - (do not remove this comment)
(script# 925)
(include sci.sh)
(use Main)
(use Print)
(use System)


(class MessageObj of Obj
	(properties
		modNum -1
		noun 0
		verb 0
		case 0
		sequence 0
		whoSays 0
		client 0
		caller 0
		font 0
		x 0
		y 0
	)
	
	(method (showSelf &tmp [temp0 40])
		(= whoSays
			(gMessager
				findTalker: (Message 0 modNum noun verb case (or sequence 1))
			)
		)
		(if (not (IsObject whoSays))
			(Print
				addTextF:
					{<MessageObj> Message not found: %d - %d, %d, %d, %d}
					modNum
					noun
					verb
					case
					sequence
				init:
			)
			(= gQuitGame 1)
		else
			(if font (whoSays font: font))
			(if (or x y) (whoSays x: x y: y))
			(gMessager say: noun verb case sequence caller modNum)
		)
	)
)

(class Conversation of List
	(properties
		elements 0
		size 0
		script 0
		curItem -1
		caller 0
	)
	
	(method (init theCaller)
		(= curItem -1)
		(if (and argc (IsObject theCaller))
			(= caller theCaller)
		)
		(gTheDoits add: self)
		(self cue:)
	)
	
	(method (doit)
		(if script (script doit:))
	)
	
	(method (dispose &tmp theCaller)
		(self eachElementDo: 96 cleanCode)
		(gTheDoits delete: self)
		(if gDialog (gDialog dispose:))
		(if script (= script 0))
		(= theCaller caller)
		(super dispose:)
		(if theCaller (theCaller cue:))
	)
	
	(method (add theTheGRoomNumber_2 theTheTheGRoomNumber_2 theTheTheTheGRoomNumber_2 theTheTheTheTheGRoomNumber_2 theTheTheTheTheTheGRoomNumber param6 param7 param8 &tmp theGRoomNumber theTheGRoomNumber theTheTheGRoomNumber theTheTheTheGRoomNumber theTheTheTheTheGRoomNumber temp5 temp6 temp7)
		(= theTheTheTheTheGRoomNumber 0)
		(= theTheTheTheGRoomNumber theTheTheTheTheGRoomNumber)
		(= theTheTheGRoomNumber theTheTheTheGRoomNumber)
		(= theTheGRoomNumber theTheTheGRoomNumber)
		(= theGRoomNumber theTheGRoomNumber)
		(= temp7 0)
		(= temp6 temp7)
		(= temp5 temp6)
		(if
		(and argc (not (IsObject theTheGRoomNumber_2)))
			(= theGRoomNumber theTheGRoomNumber_2)
			(if (== theGRoomNumber -1)
				(= theGRoomNumber gRoomNumber)
			)
			(if (> argc 1)
				(= theTheGRoomNumber theTheTheGRoomNumber_2)
				(if (> argc 2)
					(= theTheTheGRoomNumber theTheTheTheGRoomNumber_2)
					(if (> argc 3)
						(= theTheTheTheGRoomNumber theTheTheTheTheGRoomNumber_2)
						(if (> argc 4)
							(= theTheTheTheTheGRoomNumber
								theTheTheTheTheTheGRoomNumber
							)
							(if (> argc 5)
								(= temp5 param6)
								(if (> argc 6)
									(= temp6 param7)
									(if (> argc 7) (= temp7 param8))
								)
							)
						)
					)
				)
			)
			(if (not (IsObject theTheGRoomNumber_2))
				(super
					add:
						((MessageObj new:)
							modNum: theGRoomNumber
							noun: theTheGRoomNumber
							verb: theTheTheGRoomNumber
							case: theTheTheTheGRoomNumber
							sequence: theTheTheTheTheGRoomNumber
							x: temp5
							y: temp6
							font: temp7
							yourself:
						)
				)
			)
		else
			(super
				add: theTheGRoomNumber_2 &rest theTheTheGRoomNumber_2
			)
		)
	)
	
	(method (cue param1 &tmp temp0 temp1)
		(if (or (and argc param1) (== (++ curItem) size))
			(self dispose:)
		else
			(= temp0 (self at: curItem))
			(cond 
				((temp0 isKindOf: MessageObj) (temp0 caller: self showSelf:))
				((temp0 isKindOf: Script) (self setScript: temp0 self))
				((IsObject temp0) (temp0 doit: self))
				(else (self cue:))
			)
		)
	)
	
	(method (setScript param1)
		(if (IsObject script) (script dispose:))
		(if param1 (param1 init: self &rest))
	)
	
	(method (load param1 &tmp theGRoomNumber temp1 temp2 temp3 temp4 temp5 temp6 temp7 temp8)
		(= theGRoomNumber (WordAt param1 0))
		(= temp1 (WordAt param1 1))
		(= temp2 (WordAt param1 2))
		(= temp3 (WordAt param1 3))
		(= temp4 (WordAt param1 4))
		(= temp5 (WordAt param1 5))
		(= temp6 (WordAt param1 6))
		(= temp7 (WordAt param1 7))
		(= temp8 7)
		(while theGRoomNumber
			(if (== theGRoomNumber -1)
				(= theGRoomNumber gRoomNumber)
			)
			(self
				add: theGRoomNumber temp1 temp2 temp3 temp4 temp5 temp6 temp7
			)
			(= theGRoomNumber (WordAt param1 (++ temp8)))
			(= temp1 (WordAt param1 (++ temp8)))
			(= temp2 (WordAt param1 (++ temp8)))
			(= temp3 (WordAt param1 (++ temp8)))
			(= temp4 (WordAt param1 (++ temp8)))
			(= temp5 (WordAt param1 (++ temp8)))
			(= temp6 (WordAt param1 (++ temp8)))
			(= temp7 (WordAt param1 (++ temp8)))
		)
	)
)

(instance cleanCode of Code
	(properties)
	
	(method (doit param1 &tmp temp0)
		(if (param1 isKindOf: Script) (param1 caller: 0))
		(if
			(and
				(param1 isKindOf: MessageObj)
				(IsObject (= temp0 (param1 whoSays?)))
				(temp0 underBits?)
			)
			(temp0 dispose: 1)
		)
	)
)
