;;; Sierra Script 1.0 - (do not remove this comment)
(script# 924)
(include sci.sh)
(use Main)
(use Print)
(use Game)
(use System)


(class Messager of Obj
	(properties
		caller 0
		disposeWhenDone 1
		oneOnly 0
		killed 0
		oldIconBarState 0
		curSequence 0
		lastSequence 0
		talker 0
	)
	
	(method (dispose)
		(talkerSet dispose:)
		(if gIconBar
			(gIconBar state: oldIconBarState)
			(= oldIconBarState 0)
		)
		(if caller
			(if (not gCuees) (= gCuees (Set new:)))
			(gCuees
				add: ((Cue new:)
					cuee: caller
					cuer: self
					register: killed
					yourself:
				)
			)
		)
		(= talker 0)
		(super dispose:)
	)
	
	(method (cue param1)
		(if (and argc param1) (= killed 1))
		(if (or oneOnly killed)
			(if gFastCast
				(gFastCast release: dispose:)
				(= gFastCast 0)
			)
			(self dispose:)
		else
			(self sayNext:)
		)
	)
	
	(method (say theLastSequence theCaller theTheTheTheCaller theCurSequence &tmp theTheLastSequence theTheCaller theTheTheCaller temp3 [temp4 20] temp24)
		(= theTheTheCaller (= curSequence 0))
		(= theTheCaller theTheTheCaller)
		(= theTheLastSequence theTheCaller)
		(= caller (= oneOnly (= killed 0)))
		(if (and gIconBar (not oldIconBarState))
			(= oldIconBarState (gIconBar state?))
		)
		(= theTheLastSequence theLastSequence)
		(if (== theTheLastSequence -1)
			(if (and (> argc 1) (IsObject theCaller))
				(= caller theCaller)
			)
			(self sayNext:)
		else
			(if (and (> argc 1) theCaller)
				(= theTheCaller theCaller)
			)
			(if (and (> argc 2) theTheTheTheCaller)
				(= theTheTheCaller theTheTheTheCaller)
			)
			(if (and (> argc 3) theCurSequence)
				(= oneOnly 1)
				(= curSequence theCurSequence)
			else
				(= curSequence 1)
			)
			(= temp24 4)
			(if
				(and
					(> argc temp24)
					[theLastSequence temp24]
					(not (IsObject [theLastSequence temp24]))
				)
				(= lastSequence [theLastSequence temp24])
				(++ temp24)
				(= oneOnly 0)
			else
				(= lastSequence 0)
			)
			(if (and (> argc temp24) [theLastSequence temp24])
				(= caller [theLastSequence temp24])
			else
				(= caller 0)
			)
			(= temp3
				(if (> argc (++ temp24))
					[theLastSequence temp24]
				else
					gRoomNumber
				)
			)
			(if
				(and
					gMessageType
					(Message
						0
						temp3
						theTheLastSequence
						theTheCaller
						theTheTheCaller
						curSequence
					)
				)
				(self
					sayNext: temp3 theTheLastSequence theTheCaller theTheTheCaller curSequence
				)
			else
				(Print
					addTextF:
						{<Messager>\n\tmsgType set to 0 or\n\t%d: %d, %d, %d, %d not found}
						temp3
						theTheLastSequence
						theTheCaller
						theTheTheCaller
						curSequence
					init:
				)
				(self dispose:)
			)
		)
	)
	
	(method (sayFormat param1 param2 theCaller &tmp temp0 temp1 temp2)
		(if (and gIconBar (not oldIconBarState))
			(= oldIconBarState (gIconBar state?))
		)
		(= temp2 (self findTalker: param1))
		(= temp0 (FindFormatLen param2 theCaller &rest))
		(if (IsObject [theCaller (- argc 2)])
			(= caller [theCaller (- argc 2)])
		)
		(= oneOnly 1)
		(= temp1 (Memory 1 temp0))
		(Format temp1 param2 theCaller &rest)
		(temp2 say: temp1 self)
		(Memory 3 temp1)
	)
	
	(method (sayNext param1 param2 param3 param4 param5 &tmp theTalker [temp1 200] temp201)
		(if argc
			(= theTalker
				(Message 0 param1 param2 param3 param4 param5 @temp1)
			)
		else
			(= theTalker (Message 1 @temp1))
		)
		(if (& gMessageType $0002)
			(= temp201 (Memory 1 12))
			(Message 8 temp201)
		)
		(if
			(and
				theTalker
				(or
					(not lastSequence)
					(and lastSequence (<= curSequence lastSequence))
				)
			)
			(= theTalker (self findTalker: theTalker))
			(if
				(and
					talker
					(!= theTalker talker)
					(== (talker disposeWhenDone?) 2)
				)
				(talker caller: 0 dispose: 1)
			)
			(= talker theTalker)
			(if (!= theTalker -1)
				(talkerSet add: theTalker)
				(if (& gMessageType $0002)
					(theTalker modNum: param1 say: temp201 self)
				else
					(theTalker
						modNum: param1
						say: @temp1 self param1 param2 param3 param4 param5
					)
				)
				(++ curSequence)
			else
				(if gFastCast
					(gFastCast release: dispose:)
					(= gFastCast 0)
				)
				(self dispose:)
			)
		else
			(if gFastCast
				(gFastCast release: dispose:)
				(= gFastCast 0)
			)
			(self dispose:)
		)
		(if (& gMessageType $0002) (Memory 3 temp201))
	)
	
	(method (findTalker)
		(Print
			width: 200
			addText:
				{<Messager findTalker:>\n\tCan't find talker or\n\tfindTalker method not over-ridden}
			init:
		)
		(return gNarrator)
	)
)

(instance talkerSet of Set
	(properties)
	
	(method (dispose)
		(self
			eachElementDo: 143 0
			eachElementDo: 111 (gMessager disposeWhenDone?)
			release:
		)
		(super dispose:)
	)
)
