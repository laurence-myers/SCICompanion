;;; Sierra Script 1.0 - (do not remove this comment)
(script# 976)
(include sci.sh)

; A term of an and with statements before its value (Pepper's Adventures
; in Time script 230, sTalkPoorRich::changeState): the term stores 1 in a
; variable and calls a procedure, and the and tests the value of the call.
; Sierra's compiler took a group: statements in parentheses, whose value
; is the value of the last one. The cond clause has no code.
; Sierra: (cond ((and (not local0) (> param1 5) ((= local0 1) (Abs param1)))) ((not local0) (= temp0 2)))
; In the second procedure, the statement before the value is an or: its
; bt goes to the end of the or, not to the then-part of the if.
; Sierra: (if (and param1 ((or param2 param3) param3)) (= temp0 1) else (= temp0 2))
; In the third procedure, the group has a call of a text-tuple procedure
; (FormatPrint, with text 976 entry 0): the comment with the text goes to
; a statement list around the group, not after the value of the group.
(use V5_FormatPrint)

(local
	local0
)

(public
	v5GroupTerm 0
	v5GroupOrStatement 1
	v5GroupTextTuple 2
)

(procedure (v5GroupTerm param1 &tmp temp0)
	(asm
		lal local0
		not
		bnt next
		lsp param1
		ldi 5
		gt?
		bnt next
		ldi 1
		sal local0
		push1
		lsp param1
		callk Abs, 2
		bnt next
		jmp done
	next:
		lal local0
		not
		bnt done
		ldi 2
		sat temp0
	done:
		ret
	)
)

(procedure (v5GroupOrStatement param1 param2 param3 &tmp temp0)
	(asm
		lap param1
		bnt orElse
		lap param2
		bt orEnd
		lap param3
	orEnd:
		lap param3
		bnt orElse
		ldi 1
		sat temp0
		jmp orDone
	orElse:
		ldi 2
		sat temp0
	orDone:
		ret
	)
)

(procedure (v5GroupTextTuple param1 param2 &tmp temp0)
	(if (and param1 ((FormatPrint 976 0) param2))
		(= temp0 1)
	else
		(= temp0 2)
	)
)
