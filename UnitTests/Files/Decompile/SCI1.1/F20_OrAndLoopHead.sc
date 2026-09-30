;;; Sierra Script 1.0 - (do not remove this comment)
(script# 944)
(include sci.sh)

; The test of an if that ends a loop body is an or, and an and is the last
; operand of that or. When the and is false, the if's "bnt" goes to the loop
; head, so Sierra's compiler sends the and's own "bnt" straight to the loop
; head, past the if's "bnt". The or's "bt" goes to the if's "bnt".
; f20OrAndInLoop: the shape of jackalMan::initCombat (script 695) of Quest
; for Glory II. f20OrTwoAnds: an or of two ands, the shape of
; Morris::minimax (script 541) of Conquests of the Longbow.
(public
	f20OrAndInLoop 0
	f20OrTwoAnds 1
)

(procedure (f20OrAndInLoop param1 param2 &tmp temp0)
	(asm
	loopHead:
		-at temp0
		push
		ldi 0
		gt?
		bnt loopExit
		lsp param1
		ldi 0
		eq?
		bt ifTest
		lsp param1
		ldi 1
		eq?
		bnt loopHead
		lst temp0
		ldi 2
		gt?
	ifTest:
		bnt loopHead
		ldi 1
		sap param2
		jmp loopHead
	loopExit:
		ret
	)
)

(procedure (f20OrTwoAnds param1 param2 &tmp temp0 temp1)
	(asm
	loopHead2:
		lap param1
		bnt loopExit2
		lap param2
		not
		bnt firstAnd
		lst temp0
		lat temp1
		ge?
	firstAnd:
		bt ifTest2
		lap param2
		bnt loopHead2
		lst temp0
		lat temp1
		le?
	ifTest2:
		bnt loopHead2
		+at temp0
		jmp loopHead2
	loopExit2:
		lat temp0
		ret
	)
)