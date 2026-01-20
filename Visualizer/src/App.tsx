import { useEffect, useState } from 'react'
import './App.css'

enum State {
  NotReady,
  Ready
}

interface Rotation {
  k: number;
  i: number;
  j: number;
}

interface InputData {
  initialBoard: number[][];
  rotations: Rotation[];
}

function App() {
  const [data, setData] = useState<InputData | null>(null);
  const [currBoard, setCurrBoard] = useState<number[][] | null>(null);
  const [state, setState] = useState<State>(State.NotReady);
  const [currentStep, setCurrentStep] = useState<number>(0);
  const [manualStep, setManualStep] = useState<string>('');

  useEffect(() => {
    async function loadData() {
      try {
        const response = await fetch("/result.json");
        const json: any = await response.json();
        
        // Convert rotation object to array
        const rotationsArray: Rotation[] = [];
        if (json.rotation) {
          const keys = Object.keys(json.rotation).map(Number).sort((a, b) => a - b);
          keys.forEach(key => {
            rotationsArray.push(json.rotation[key]);
          });
        }
        
        const processedData: InputData = {
          initialBoard: json.initialBoard,
          rotations: rotationsArray
        };
        
        setData(processedData);
        setCurrBoard(processedData.initialBoard);
      } catch (error) {
        console.log(error);
      }
    }
    loadData();
  }, []);

  useEffect(() => {
    let timer: number;
    if (state === State.Ready && data) {
      timer = window.setInterval(() => {
        setCurrentStep((prev) => {
          if (prev < data.rotations.length) {
            return prev + 1;
          } else {
            setState(State.NotReady);
            return prev;
          }
        });
      }, 200)
    }
    return () => { clearInterval(timer) };
  }, [state, data])

  useEffect(() => {
    if (!data || !data.rotations || !data.initialBoard) return;
    
    let board = data.initialBoard.map(row => [...row]);
    
    for (let step = 0; step < currentStep; step++) {
      const rotation = data.rotations[step];
      if (rotation) {
        board = applyRotation(board, rotation.k, rotation.i, rotation.j);
      }
    }
    
    setCurrBoard(board);
  }, [currentStep, data]);

  const applyRotation = (board: number[][], k: number, i: number, j: number): number[][] => {
    const temp = board.map(row => [...row]);
    const buffer = Array.from({ length: k }, () => Array(k).fill(0));
    
    for (let x = 0; x < k; x++) {
      for (let y = 0; y < k; y++) {
        buffer[y][k - 1 - x] = temp[i + x][j + y];
      }
    }
    
    for (let x = 0; x < k; x++) {
      for (let y = 0; y < k; y++) {
        temp[i + x][j + y] = buffer[x][y];
      }
    }
    
    return temp;
  }

  const rotationsSize = data?.rotations?.length ?? 0;

  const handlePrevClick = () => {
    setState(State.NotReady);
    setCurrentStep((prev) => {
      if (prev > 0) {
        return prev - 1
      }
      return prev;
    });
  }

  const handleNextClick = () => {
    setState(State.NotReady);
    setCurrentStep((prev) => {
      if (prev < rotationsSize) {
        return prev + 1
      }
      return prev;
    });
  }

  const handleAutoClick = () => {
    if (state === State.Ready) {
      setState(State.NotReady);
    } else {
      setState(State.Ready);
    }
  }

  const handleManualStepChange = (e: React.ChangeEvent<HTMLInputElement>) => {
    setManualStep(e.target.value);
  }

  const handleManualStepSubmit = () => {
    const step = parseInt(manualStep);
    if (!isNaN(step) && step >= 0 && step <= rotationsSize) {
      setState(State.NotReady);
      setCurrentStep(step);
      setManualStep('');
    }
  }

  return (
    <div className="container">
      <div className="step-counter">
        <p>Step: {currentStep} / {rotationsSize}</p>
      </div>
      <div className="controls">
        <button onClick={handlePrevClick}>Prev</button>
        <button onClick={handleNextClick}>Next</button>
        <button onClick={handleAutoClick}>
          {state === State.Ready ? 'Pause' : 'Auto'}
        </button>
        <input 
          type="number" 
          placeholder="Enter step number"
          value={manualStep}
          onChange={handleManualStepChange}
        />
        <button onClick={handleManualStepSubmit}>Go</button>
      </div>
      <div className="board-container">
        {currBoard?.map((row, i) => (
          <div key={i} className="board-row">
            {row.map((cell, j) => {
              const isHorizontalPair = (j < row.length - 1 && row[j + 1] === cell) || 
                                       (j > 0 && row[j - 1] === cell);
              
              const isVerticalPair = (i < currBoard.length - 1 && currBoard[i + 1][j] === cell) || 
                                     (i > 0 && currBoard[i - 1][j] === cell);
              
              const isPaired = isHorizontalPair || isVerticalPair;
              
              let isInRotation = false;
              if (currentStep >= 0 && data && currentStep <= data.rotations.length) {
                const rotation = data.rotations[currentStep];
                isInRotation = i >= rotation.i && i < rotation.i + rotation.k &&
                               j >= rotation.j && j < rotation.j + rotation.k;
              }
              
              return (
                <span 
                  key={j} 
                  className={`board-cell ${isInRotation ? 'rotating' : isPaired ? 'paired' : ''}`}
                >
                  {cell}
                </span>
              );
            })}
          </div>
        ))}
      </div>
    </div>
  )
}

export default App