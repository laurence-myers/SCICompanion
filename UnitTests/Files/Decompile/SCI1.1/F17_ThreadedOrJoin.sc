;;; Sierra Script 1.0 - (do not remove this comment)
(script# 939)
(include sci.sh)

; An or that is the last operand of an and, where the and is the first
; operand of an outer or. The inner or ends where the and ends, at the outer
; or's "bt", so Sierra's compiler sends the inner or's "bt" past it, straight
; to the outer or's end: the accumulator is true there, so that "bt" is
; taken too.
; f17ThreadedOr: the inner or's first operand is an and whose last operand
; is an assignment. Shape from King's Quest VI, script 850 (studyDoor::onMe).
; f17ThreadedOrThree: the inner or has three operands, so two of its "bt"s
; go past the join. Shape from Space Quest III, script 255
; (Dialog::handleEvent).
(public
	f17ThreadedOr 0
	f17ThreadedOrThree 1
)

(procedure (f17ThreadedOr param1 param2 param3 &tmp temp0)
	(asm
		lap param1
		bnt done
		lap param2
		bnt andEnd
		lap param3
		bnt innerAnd
		ldi 5
		sat temp0
	innerAnd:
		bt done
		ldi 6
		sat temp0
	andEnd:
		bt done
		ldi 1
	done:
		ret
	)
)

(procedure (f17ThreadedOrThree param1 param2 param3 param4 param5)
	(asm
		lap param1
		bnt threeAndEnd
		lap param2
		bt threeDone
		lap param3
		bt threeDone
		lap param4
	threeAndEnd:
		bt threeDone
		lap param5
	threeDone:
		ret
	)
)
