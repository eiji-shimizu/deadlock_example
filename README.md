# DeadLock Example

<br>
データベースのデッドロックを手軽に体験できるプログラム<br>
<br>

## 概略

実行ファイル内に簡単なWebサーバーとデータベースが含まれている<br>
ブラウザからアクセスして更新操作を入力することで<br>
データベースのデッドロックを手軽に体験できる<br>
<br>

## ビルド

CMakeを利用する<br>
コンパイラはVisual C++となる<br>

1. リポジトリをクローン
2. deadlock_exampleフォルダ直下にbuildフォルダを作成
3. buildフォルダに移動(cd build)
4. 下記コマンドを実行(Visual Studioのバージョンは実際の環境に合わせる)
    - cmake -G "Visual Studio 17 2022" -A "x64" -DCMAKE_BUILD_TYPE=Release ..
    - cmake --build . --config Release
5. 環境によるかもしれないがbuildフォルダ内のsrc\Releaseフォルダに実行ファイルができる
6. 次のフォルダ階層を作成する
    - 例えばCドライブ直下にdeadlock_exampleフォルダを作成する
    - クローンしたリポジトリからdatabaseフォルダ,sitesフォルダ,webconfigフォルダ及びその中身をコピーする
    - databaseフォルダの中にdataフォルダを作成して、その中にorder,userという名前で空のファイルを作成する

> deadlock_example<br>
> │  deadlock_example.exe<br>
> │<br>
> ├─database<br>
> │  │  tables.ini<br>
> │  │<br>
> │  └─data<br>
> │          &emsp;&emsp;&emsp;order<br>
> │          &emsp;&emsp;&emsp;user<br>
> │<br>
> ├─sites<br>
> │  │  favicon.ico<br>
> │  │  index.html<br>
> │  │<br>
> │  ├─dlex<br>
> │  │  │  favicon.ico<br>
> │  │  │  top.html<br>
> │  │  │<br>
> │  │  ├─css<br>
> │  │  │      dlex.css<br>
> │  │  │<br>
> │  │  └─js<br>
> │  │          &emsp;dlex.js<br>
> │  │<br>
> │  └─helloworld<br>
> │&emsp;&emsp;│  favicon.ico<br>
> │&emsp;&emsp;│  top.html<br>
> │&emsp;&emsp;│<br>
> │&emsp;&emsp;└─img<br>
> │              &emsp;&emsp;&emsp;irukakun.png<br>
> │<br>
> └─webconfig<br>
>         &emsp;&emsp;&emsp;server.ini<br>
<br>
<br>

## 機能

実行すると以下のURLでトップページにアクセスできる<br>
http://localhost:27015/dlex/top.html<br>
操作例

1. 受注名「order1」, &nbsp;お客様「お客様A」, &nbsp;商品名「テスト商品１」で登録
2. 受注名「order2」, &nbsp;お客様「お客様B」, &nbsp;商品名「テスト商品２」で登録
3. 更新操作1に以下の順序で入力
    1. 更新する受注名「order1」, &nbsp;商品名「テスト商品A」
    2. 更新する受注名「order2」, &nbsp;商品名「テスト商品B」
4. 更新操作2に以下の順序で入力
    1. 更新する受注名「order2」, &nbsp;商品名「テスト商品C」
    2. 更新する受注名「order1」, &nbsp;商品名「テスト商品D」
5. 操作1実行と操作2実行を素早く押すとデッドロックになる
6. コンソールにdコマンド入力で解除できる
7. 3.と4.で更新する受注名の順番を同じにすればデッドロックは起きない

コンソール入力

- dコマンド:&nbsp;dを入力してEnterですべてのトランザクションが停止する(デッドロック時の解除に使う)
- qコマンド:&nbsp;qを入力してEnterでアプリケーションが終了する

