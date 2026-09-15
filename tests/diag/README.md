# 에러 메시지 회귀 테스트

여기 있는 `*.my` 는 **일부러 틀린 프로그램**이다. 각 파일 옆의 `*.expected` 에
인터프리터가 내야 할 출력이 그대로 들어 있고, `tests/run_tests.sh` 가 비교한다.

에러 메시지는 Venos 의 제품 기능이다 (초보자가 처음 만나는 화면이 대부분 에러다).
문구를 고치면 `.expected` 도 같이 고칠 것 — 그게 "일부러 바꿨다"는 표시가 된다.

`.expected` 를 다시 만들려면:

    ./venos tests/diag/오타_변수.my > tests/diag/오타_변수.expected 2>&1
