// ============================================================
//  Venos 레슨 데이터 — 이 파일이 튜토리얼의 단일 진실 공급원이다.
//  - 플레이그라운드(docs/index.html)가 그대로 읽어 레슨 트랙을 그린다.
//  - TUTORIAL.md / TUTORIAL.ko.md 는 tools/gen-tutorial.js 가 여기서 생성한다.
//  레슨을 고치면 생성기를 다시 돌릴 것:  node tools/gen-tutorial.js
//
//  각 레슨: id(URL용) / title·desc·code 전부 ko·en (코드는 5~15줄, 양쪽 다 에러 없이 실행돼야 함)
//  순서는 VENOS_SPEC 의 서술 순서를 따른다.
// ============================================================
const LESSONS = [
  {
    id: 'print',
    title: { ko: '화면에 출력하기', en: 'Printing' },
    desc: {
      ko: '`print` 는 값을 화면에 보여줍니다. 쉼표로 여러 개를 한 줄에 쓸 수 있어요. `#` 뒤는 주석이라 실행되지 않습니다.\n\n▶ Run 을 눌러보고, 따옴표 안의 글자를 바꿔 다시 실행해 보세요.',
      en: '`print` shows a value on screen. Separate several values with commas to put them on one line. Anything after `#` is a comment and is not run.\n\nPress ▶ Run, then change the text inside the quotes and run it again.'
    },
    code: {
      ko: `# 첫 번째 Venos 프로그램
print "안녕하세요!"
print "반가워요 :)"

# 쉼표로 여러 값을 한 줄에
print "1 + 2 =", 1 + 2`,
      en: `# first Venos program
print "Hello!"
print "Hi :)"

# print multiple values with commas
print "1 + 2 =", 1 + 2`
    }
  },
  {
    id: 'variables',
    title: { ko: '변수 — 값에 이름 붙이기', en: 'Variables' },
    desc: {
      ko: '`let` 으로 변수를 만듭니다. **변수 이름은 한국어로 지어도 됩니다.**\n\n한 번 만든 뒤에는 `let` 없이 값을 바꿀 수 있고, `+=` 로 더할 수도 있어요.\n\n▶ `나이` 를 바꿔서 실행해 보세요.',
      en: 'Create a variable with `let`. **Names can be written in Korean** (or any language).\n\nOnce created, assign to it without `let`, and use `+=` to add to it.\n\n`이름` means "name" and `나이` means "age" — the identifiers stay Korean here on purpose. Try changing `나이` and running it again.'
    },
    code: {
      ko: `let 이름 = "미르"
let 나이 = 15

print 이름
print 나이

나이 += 1          # 한 살 더
print "내년 나이:", 나이`,
      en: `let 이름 = "미르"
let 나이 = 15

print 이름
print 나이

나이 += 1          # age + 1
print "next year:", 나이`
    }
  },
  {
    id: 'input',
    title: { ko: '입력받기와 문자열 보간', en: 'Input and string interpolation' },
    desc: {
      ko: '`input` 은 사용자에게 값을 물어봅니다. 플레이그라운드에서는 출력창 아래에 입력줄이 나타납니다.\n\n문자열 안에 `{ }` 를 쓰면 그 안의 값이 그대로 끼워집니다 — 이걸 **문자열 보간**이라고 해요. `+` 로 이어붙이는 것보다 읽기 쉽습니다.\n\n▶ 실행하면 이름을 물어봅니다. 아무 이름이나 넣어 보세요.',
      en: '`input` asks the user for a value — in the playground an input line appears below the output.\n\nInside a string, `{ }` inserts the value in it. That is **string interpolation**, and it reads better than joining with `+`.\n\nRun it and type any name into the input line, then press Enter.'
    },
    code: {
      ko: `let 이름 = input "이름이 뭐예요? "
let 나이 = 15

print "안녕하세요, {이름}님!"
print "{이름}님은 내년에 {나이 + 1}살이 되네요."`,
      en: `let name = input "What is your name? "
let age = 15

print "Hello, {name}!"
print "{name} becomes {age + 1} next year."`
    }
  },
  {
    id: 'if',
    title: { ko: '조건 — if / else', en: 'Conditions' },
    desc: {
      ko: '`if` 는 조건이 참일 때만 `{ }` 안을 실행합니다. 아니면 `else if`, 그것도 아니면 `else` 로 넘어가요.\n\n`then` 은 써도 되고 안 써도 됩니다 — 읽기 좋으라고 있는 단어예요.\n\n▶ `점수` 를 바꿔가며 실행해 등급이 어떻게 달라지는지 보세요.',
      en: '`if` runs the block only when the condition is true; otherwise it falls through to `else if`, then `else`.\n\n`then` is optional — it is there only to make the line read like a sentence.\n\nChange `score` and see how the grade changes.'
    },
    code: {
      ko: `let 점수 = 85

if 점수 >= 90 then {
    print "A 등급"
} else if 점수 >= 80 {
    print "B 등급"
} else {
    print "더 힘내요!"
}

print "점수는 {점수}점입니다."`,
      en: `let score = 85

if score >= 90 then {
    print "grade A"
} else if score >= 80 {
    print "grade B"
} else {
    print "Better next time!"
}

print "The score is {score}"`
    }
  },
  {
    id: 'for',
    title: { ko: '반복 — for', en: 'Repeating with for' },
    desc: {
      ko: '`for i = 1 to 5` 는 1부터 5까지 **양 끝을 포함해서** 반복합니다.\n\n`step` 으로 건너뛸 수도 있어요. 거꾸로 세려면 `step -1` 처럼 음수를 씁니다.\n\n`"*" * i` 는 별을 i 개 이어붙인 문자열이라 반복문으로 그림을 그릴 수 있어요.\n\n▶ 숫자를 바꿔 구구단을 다른 단으로 바꿔 보세요.',
      en: '`for i = 1 to 5` repeats from 1 to 5, **including both ends**.\n\nUse `step` to skip, and a negative step to count down.\n\n`"*" * i` is a string of i stars, so a loop can draw with it.\n\nChange the numbers to print a different multiplication table.'
    },
    code: {
      ko: `# 3단 구구단
for i = 1 to 9 {
    print "3 x {i} = {3 * i}"
}

print ""
# 거꾸로 세기
for i = 5 to 1 step -1 {
    print i
}
print "발사!"

print ""
# 별 쌓기
for i = 1 to 5 { print "*" * i }`,
      en: `# 3 times table
for i = 1 to 9 {
    print "3 x {i} = {3 * i}"
}

print ""
# backwards
for i = 5 to 1 step -1 {
    print i
}
print "Liftoff!"

print ""
# a staircase of stars
for i = 1 to 5 { print "*" * i }`
    }
  },
  {
    id: 'while',
    title: { ko: '조건이 참인 동안 — while', en: 'Repeating with while' },
    desc: {
      ko: '`while` 은 조건이 참인 **동안** 계속 반복합니다. 몇 번 반복할지 미리 모를 때 씁니다.\n\n`break` 로 즉시 빠져나올 수 있어요. 조건을 언젠가 거짓으로 만들지 않으면 영원히 돌게 되니 주의하세요.\n\n▶ `남은돈` 을 바꿔 몇 번 살 수 있는지 보세요.',
      en: '`while` keeps repeating **as long as** the condition is true — use it when you do not know the count in advance.\n\n`break` leaves the loop immediately. Make sure the condition eventually becomes false, or the loop never ends.\n\nChange `moneyleft` and see how many you can buy.'
    },
    code: {
      ko: `let 남은돈 = 1000
let 가격 = 300
let 개수 = 0

while 남은돈 >= 가격 do {
    남은돈 -= 가격
    개수 += 1
}

print "{개수}개 샀고, {남은돈}원 남았어요."`,
      en: `let moneyleft = 50
let price = 15
let count = 0

while moneyleft >= price do {
    moneyleft -= price
    count += 1
}

print "Bought {count}, with {moneyleft} dollars left."`
    }
  },
  {
    id: 'lists',
    title: { ko: '리스트 — 값을 여러 개', en: 'Lists' },
    desc: {
      ko: '리스트는 값을 순서대로 담습니다. **번호는 1부터 시작해요** (0이 아닙니다!).\n\n`push` 로 뒤에 추가하고, `len` 으로 개수를 세고, `for ... in` 으로 하나씩 꺼내 봅니다.\n\n▶ 과일을 더 추가해 보세요.',
      en: 'A list holds values in order. **Indices start at 1**, not 0!\n\n`push` appends, `len` counts, and `for ... in` walks through the items.\n\nTry adding more fruit to the list.'
    },
    code: {
      ko: `let 과일 = ["사과", "바나나", "포도"]

print "첫 번째:", 과일[1]      # 1번부터!
print "개수:", len(과일)

push(과일, "딸기")

for 하나 in 과일 {
    print "-", 하나
}`,
      en: `let fruit = ["apple", "banana", "grape"]

print "first:", fruit[1]      # counting starts at 1!
print "count:", len(fruit)

push(fruit, "strawberry")

for one in fruit {
    print "-", one
}`
    }
  },
  {
    id: 'dicts',
    title: { ko: '딕셔너리 — 이름표가 붙은 값', en: 'Dictionaries' },
    desc: {
      ko: '딕셔너리는 **이름표(키)** 로 값을 찾습니다. 키는 항상 문자열이에요.\n\n없는 키를 읽으면 에러가 나니, `has` 로 먼저 확인하는 습관을 들이세요.\n\n▶ 과목과 점수를 더 넣어 보세요.',
      en: 'A dictionary looks values up by a **key**, and keys are always strings.\n\nReading a missing key is an error, so get into the habit of checking with `has` first.\n\nTry adding more subjects and scores.'
    },
    code: {
      ko: `let 성적 = {"수학": 90, "영어": 85}

성적["과학"] = 95        # 새 키는 넣으면 생김
성적["수학"] += 5

for 과목 in 성적 {
    print "{과목} → {성적[과목]}점"
}

if has(성적, "체육") {
    print "체육:", 성적["체육"]
} else {
    print "체육 점수는 아직 없어요."
}`,
      en: `let scores = {"math": 90, "english": 85}

scores["science"] = 95        # assigning a new key creates it
scores["math"] += 5

for subject in scores {
    print "{subject} -> {scores[subject]}"
}

if has(scores, "music") {
    print "music:", scores["music"]
} else {
    print "No music score yet."
}`
    }
  },
  {
    id: 'functions',
    title: { ko: '함수 — 이름 붙인 동작', en: 'Functions' },
    desc: {
      ko: '`func` 로 동작에 이름을 붙여 두면 몇 번이든 다시 쓸 수 있습니다. `return` 으로 결과를 돌려줘요.\n\n함수는 **자기 자신을 부를 수도** 있습니다(재귀). 아래 `팩토리얼` 이 그 예예요.\n\n▶ `인사("이름")` 을 한 줄 더 추가해 보세요.',
      en: 'Give a piece of behavior a name with `func` and reuse it as often as you like; `return` hands a result back.\n\nA function can even **call itself** (recursion) — `factorial` below does exactly that.\n\nTry adding one more `greet("...")` line.'
    },
    code: {
      ko: `func 인사(이름) {
    print "안녕하세요, {이름}님!"
}

func 팩토리얼(n) {
    if n <= 1 then { return 1 }
    return n * 팩토리얼(n - 1)
}

인사("미르")
인사("하늘")
print "5! =", 팩토리얼(5)`,
      en: `func greet(name) {
    print "Hello, {name}!"
}

func factorial(n) {
    if n <= 1 then { return 1 }
    return n * factorial(n - 1)
}

greet("Mir")
greet("Sky")
print "5! =", factorial(5)`
    }
  },
  {
    id: 'classes',
    title: { ko: '클래스 — 나만의 자료형', en: 'Classes' },
    desc: {
      ko: '클래스는 **값과 동작을 한 덩어리로** 묶습니다. `init` 은 만들어질 때 자동으로 불리는 생성자예요.\n\n`self` 는 "나 자신"을 가리킵니다. `self.이름` 처럼 자기 값을 읽고 쓰죠.\n\n▶ 강아지를 한 마리 더 만들어 보세요.',
      en: 'A class bundles **data and behavior** together. `init` is the constructor, called automatically when you create one.\n\n`self` means "this object" — use `self.name` to read and write its own fields.\n\nTry creating one more dog.'
    },
    code: {
      ko: `class 강아지 {
    func init(이름) {
        self.이름 = 이름
        self.나이 = 0
    }
    func 짖기() { print "{self.이름}: 멍멍!" }
    func 생일() {
        self.나이 += 1
        print "{self.이름}(은)는 이제 {self.나이}살"
    }
}

let 뭉치 = 강아지("뭉치")
뭉치.짖기()
뭉치.생일()`,
      en: `class Dog {
    func init(name) {
        self.name = name
        self.age = 0
    }
    func bark() { print "{self.name}: woof!" }
    func birthday() {
        self.age += 1
        print "{self.name} is now {self.age}"
    }
}

let buddy = Dog("Buddy")
buddy.bark()
buddy.birthday()`
    }
  },
  {
    id: 'errors',
    title: { ko: '에러 다루기 — try / catch', en: 'Handling errors' },
    desc: {
      ko: '에러가 나면 프로그램이 멈춥니다. `try` 안에서 에러가 나면 프로그램을 멈추는 대신 `catch` 로 넘어가요.\n\n에러 메시지는 `catch` 뒤에 적은 변수에 문자열로 담깁니다. `error(...)` 로 직접 에러를 낼 수도 있어요.\n\n▶ 나누는 수를 0이 아닌 값으로 바꿔 실행해 보세요.',
      en: 'An error stops the program. Inside `try`, an error jumps to `catch` instead of stopping everything.\n\nThe message lands in the variable you name after `catch`, as a string. You can raise your own with `error(...)`.\n\nTry changing the divisor to something other than 0.'
    },
    code: {
      ko: `let 나누는수 = 0

try {
    print 10 / 나누는수
    print "이 줄은 실행되지 않아요"
} catch 오류 {
    print "문제가 생겼어요:", 오류
}

print "그래도 프로그램은 계속됩니다."`,
      en: `let divisor = 0

try {
    print 10 / divisor
    print "this line never runs"
} catch err {
    print "Something went wrong:", err
}

print "The program keeps going."`
    }
  },
  {
    id: 'project',
    title: { ko: '미니 프로젝트 — 숫자 맞히기', en: 'Mini project: number guessing' },
    desc: {
      ko: '지금까지 배운 걸 전부 씁니다 — 변수, `while`, `if`, `input`, 함수, 문자열 보간.\n\n`random(1, 50)` 은 1부터 50 사이 숫자를 아무거나 고릅니다.\n\n▶ 범위를 넓히거나, 시도 횟수 제한을 넣거나, 힌트를 더 친절하게 만들어 보세요. 완성했으면 **Share** 버튼으로 친구에게 보내보세요!',
      en: 'This uses everything so far — variables, `while`, `if`, `input`, functions, and interpolation.\n\n`random(1, 50)` picks any number from 1 to 50.\n\nWiden the range, add a limit on tries, or make the hints friendlier. When it works, send it to a friend with the **Share** button!'
    },
    code: {
      ko: `let 정답 = random(1, 50)
let 시도 = 0

print "1부터 50 사이 숫자를 맞혀보세요!"

while true {
    let 답 = input "숫자: "
    시도 += 1

    if 답 == 정답 {
        print "정답! {시도}번 만에 맞혔어요 🎉"
        break
    } else if 답 < 정답 { print "더 큰 수예요 ↑" }
    else { print "더 작은 수예요 ↓" }
}`,
      en: `let answer = random(1, 50)
let tries = 0

print "Guess a number between 1 and 50!"

while true {
    let guess = input "number: "
    tries += 1

    if guess == answer {
        print "Correct! You got it in {tries} tries"
        break
    } else if guess < answer { print "Higher" }
    else { print "Lower" }
}`
    }
  }
];

// 브라우저에서는 전역 LESSONS, Node(생성기)에서는 module.exports 로 같은 데이터를 쓴다
if (typeof module !== 'undefined' && module.exports) module.exports = LESSONS;
